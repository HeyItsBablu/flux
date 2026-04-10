#ifndef FLUX_STATE_HPP
#define FLUX_STATE_HPP

#include <cassert>
#include <functional>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <vector>

// ============================================================================
// STATE CLASS - REACTIVE STATE MANAGEMENT
// ============================================================================

class FluxUI;

template <typename T> class State {
private:
  T value;
  FluxUI *ui;
  std::vector<std::weak_ptr<Widget>> observers;
  std::vector<std::function<void(T)>> listeners;

  struct PropertyBinding {
    std::weak_ptr<Widget> widget;
    std::function<void(Widget *, const T &)> applier;
    bool needsLayout;
  };
  std::vector<PropertyBinding> propertyBindings;

  mutable std::mutex stateMutex;

  // ========================================================================
  // PRIVATE HELPERS
  // ========================================================================

  template <typename U = T>
  typename std::enable_if<std::is_integral<U>::value &&
                              !std::is_same<U, bool>::value,
                          std::string>::type
  valueToString(const U &val) const {
    return std::to_string(val);
  }

  template <typename U = T>
  typename std::enable_if<std::is_floating_point<U>::value, std::string>::type
  valueToString(const U &val) const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << val;
    return oss.str();
  }

  template <typename U = T>
  typename std::enable_if<std::is_same<U, bool>::value, std::string>::type
  valueToString(const U &val) const {
    return val ? "true" : "false";
  }

  template <typename U = T>
  typename std::enable_if<std::is_same<U, std::string>::value,
                          std::string>::type
  valueToString(const U &val) const {
    return val;
  }

  template <typename U = T>
  typename std::enable_if<
      !std::is_integral<U>::value && !std::is_floating_point<U>::value &&
          !std::is_same<U, bool>::value && !std::is_same<U, std::string>::value,
      std::string>::type
  valueToString(const U &) const {
    return "[complex type]";
  }

  void notifyObserversLocked() {
    std::string newText = valueToString(value);

    // Clean up expired text observers
    observers.erase(std::remove_if(observers.begin(), observers.end(),
                                   [](const std::weak_ptr<Widget> &w) {
                                     return w.expired();
                                   }),
                    observers.end());

    for (auto &weakWidget : observers) {
      if (auto widget = weakWidget.lock()) {
        widget->text = newText;
        if (ui)
          ui->updateWidget(widget.get());
      }
    }

    // Clean up expired property bindings
    propertyBindings.erase(std::remove_if(propertyBindings.begin(),
                                          propertyBindings.end(),
                                          [](const PropertyBinding &b) {
                                            return b.widget.expired();
                                          }),
                           propertyBindings.end());

    for (auto &binding : propertyBindings) {
      if (auto widget = binding.widget.lock()) {
        binding.applier(widget.get(), value);
        if (ui) {
          if (binding.needsLayout)
            ui->partialRebuild(widget.get());
          else
            ui->invalidateWidget(widget.get());
        }
      }
    }
  }

  std::vector<std::function<void(T)>> snapshotListeners() const {
    return listeners;
  }

  void dispatchListeners(const std::vector<std::function<void(T)>> &snap,
                         const T &val) {
    for (const auto &fn : snap) {
      if (fn) {
        try {
          fn(val);
        } catch (const std::exception &e) {
#ifdef FLUX_DEBUG
          std::cerr << "[FluxUI] Listener error: " << e.what() << std::endl;
#endif
        } catch (...) {
#ifdef FLUX_DEBUG
          std::cerr << "[FluxUI] Listener threw unknown exception" << std::endl;
#endif
        }
      }
    }
  }

public:
  // ========================================================================
  // CONSTRUCTORS
  // ========================================================================

  State(T initial, FluxUI *app) : value(std::move(initial)), ui(app) {
    assert(ui != nullptr && "State requires valid FluxUI context");
  }

  State(T initial) : value(std::move(initial)), ui(nullptr) {
    ui = FluxUI::getCurrentInstance();
#ifdef FLUX_DEBUG
    if (!ui)
      std::cerr << "[FluxUI] Warning: State created without valid context"
                << std::endl;
#endif
  }

  State(State &&other) noexcept {
    std::lock_guard<std::mutex> lock(other.stateMutex);
    value = std::move(other.value);
    ui = other.ui;
    observers = std::move(other.observers);
    listeners = std::move(other.listeners);
    propertyBindings = std::move(other.propertyBindings);
    other.ui = nullptr;
  }

  // Move assignment — lock both to avoid races on either side.
  State &operator=(State &&other) noexcept {
    if (this != &other) {
      std::lock(stateMutex, other.stateMutex);
      std::lock_guard<std::mutex> l1(stateMutex, std::adopt_lock);
      std::lock_guard<std::mutex> l2(other.stateMutex, std::adopt_lock);

      value = std::move(other.value);
      ui = other.ui;
      observers = std::move(other.observers);
      listeners = std::move(other.listeners);
      propertyBindings = std::move(other.propertyBindings);
      other.ui = nullptr;
    }
    return *this;
  }

  State(const State &) = delete;
  State &operator=(const State &) = delete;

  // ========================================================================
  // CORE API
  // ========================================================================

  /**
   * get() — thread-safe snapshot of the current value.
   *
   *   int v = counter.get();
   */
  T get() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return value;
  }

  /**
   * set() — replace value, notify observers + listeners.
   *   counter.set(42);
   */
  void set(T newValue) {
    std::vector<std::function<void(T)>> snap;
    T dispatchValue;
    {
      std::lock_guard<std::mutex> lock(stateMutex);
      value = std::move(newValue);
      dispatchValue = value;
      notifyObserversLocked();
      snap = snapshotListeners();
    } // lock released here — before any listener fires

    dispatchListeners(snap, dispatchValue);
  }

  /**
   * update() — mutate via function, notify only if value changed.
   *
   *   counter.update([](int v) { return v + 1; });
   *   toggle.update([](bool v) { return !v; });
   */
  void update(std::function<T(T)> updater) {
    std::vector<std::function<void(T)>> snap;
    T dispatchValue;
    {
      std::lock_guard<std::mutex> lock(stateMutex);
      T newValue = updater(value);
      if (value == newValue)
        return;
      value = std::move(newValue);
      dispatchValue = value;
      notifyObserversLocked();
      snap = snapshotListeners();
    }

    dispatchListeners(snap, dispatchValue);
  }

  /**
   * listen() — register a change callback (thread-safe).
   *
   *   counter.listen([](int v) { std::cout << v << "\n"; });
   */
  void listen(std::function<void(T)> listener) {
    if (!listener)
      return;
    std::lock_guard<std::mutex> lock(stateMutex);
    listeners.push_back(std::move(listener));
  }

  void bindProperty(std::shared_ptr<Widget> widget,
                    std::function<void(Widget *, const T &)> applier,
                    bool needsLayout = false) {
    if (!widget)
      return;
    std::lock_guard<std::mutex> lock(stateMutex);
    propertyBindings.push_back({widget, std::move(applier), needsLayout});
    propertyBindings.back().applier(widget.get(), value);
  }

  // ========================================================================
  // OBSERVER MANAGEMENT
  // ========================================================================

  void addObserver(std::shared_ptr<Widget> widget) {
    if (!widget)
      return;
    std::lock_guard<std::mutex> lock(stateMutex);
    observers.push_back(widget);
    widget->text = valueToString(value);
  }

  void removeObserver(Widget *widget) {
    if (!widget)
      return;
    std::lock_guard<std::mutex> lock(stateMutex);
    observers.erase(std::remove_if(observers.begin(), observers.end(),
                                   [widget](const std::weak_ptr<Widget> &w) {
                                     if (auto locked = w.lock())
                                       return locked.get() == widget;
                                     return true;
                                   }),
                    observers.end());
  }

  void cleanupExpiredObservers() {
    std::lock_guard<std::mutex> lock(stateMutex);
    observers.erase(std::remove_if(observers.begin(), observers.end(),
                                   [](const std::weak_ptr<Widget> &w) {
                                     return w.expired();
                                   }),
                    observers.end());
  }

  // ========================================================================
  // UTILITY
  // ========================================================================

  std::string toString() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return valueToString(value);
  }

  bool hasContext() const { return ui != nullptr; }
  FluxUI *getContext() const { return ui; }

  size_t observerCount() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    size_t n = 0;
    for (const auto &w : observers)
      if (!w.expired())
        ++n;
    return n;
  }

  size_t listenerCount() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return listeners.size();
  }

  void clearListeners() {
    std::lock_guard<std::mutex> lock(stateMutex);
    listeners.clear();
  }
};

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

template <typename T> inline State<T> &deref(State<T> *state) { return *state; }

template <typename R, typename... States> class ComputedState {
private:
  std::function<R(States...)> computer;
  std::tuple<States *...> dependencies;

  mutable R cachedValue{};
  mutable bool dirty = true;
  mutable std::mutex mutex;

  std::shared_ptr<bool> alive = std::make_shared<bool>(true);

public:
  ComputedState(std::function<R(States...)> comp, States &...deps)
      : computer(std::move(comp)), dependencies(&deps...) {
    addListenersImpl(std::index_sequence_for<States...>{});
  }

  ~ComputedState() { *alive = false; }

  ComputedState(const ComputedState &) = delete;
  ComputedState &operator=(const ComputedState &) = delete;
  ComputedState(ComputedState &&) = delete;
  ComputedState &operator=(ComputedState &&) = delete;

  R get() const {
    std::lock_guard<std::mutex> lock(mutex);
    if (dirty) {
      cachedValue = computeImpl(std::index_sequence_for<States...>{});
      dirty = false;
    }
    return cachedValue;
  }

private:
  template <size_t... Is> void addListenersImpl(std::index_sequence<Is...>) {
    std::weak_ptr<bool> weakAlive = alive;
    std::mutex *mtx = &mutex;
    bool *d = &dirty;

    auto markDirty = [weakAlive, mtx, d](auto) {
      if (auto a = weakAlive.lock(); a && *a) {
        std::lock_guard<std::mutex> lk(*mtx);
        *d = true;
      }
    };

    (std::get<Is>(dependencies)->listen(markDirty), ...);
  }

  template <size_t... Is> R computeImpl(std::index_sequence<Is...>) const {
    return computer(std::get<Is>(dependencies)->get()...);
  }
};

#endif // FLUX_STATE_HPP