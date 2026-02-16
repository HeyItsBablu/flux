#ifndef FLUX_STATE_HPP
#define FLUX_STATE_HPP

#include "flux_core.hpp"
#include <cassert>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <vector>

// ============================================================================
// STATE CLASS - REACTIVE STATE MANAGEMENT
// ============================================================================

template <typename T> class State {
private:
  T value;
  FluxUI *ui;
  std::vector<std::weak_ptr<Widget>> observers;
  std::vector<std::function<void(T)>> listeners;

  struct PropertyBinding {
    std::weak_ptr<Widget> widget;
    std::function<void(Widget *, const T &)> applier; // applies value to widget
    bool needsLayout; // true = re-layout, false = repaint only
  };
  std::vector<PropertyBinding> propertyBindings;

  // Thread safety
  mutable std::mutex stateMutex;

  // ========================================================================
  // PRIVATE HELPERS
  // ========================================================================

  // Helper to convert value to string for display - integers
  template <typename U = T>
  typename std::enable_if<std::is_integral<U>::value &&
                              !std::is_same<U, bool>::value,
                          std::string>::type
  valueToString(const U &val) const {
    return std::to_string(val);
  }

  // Helper to convert value to string for display - floats
  template <typename U = T>
  typename std::enable_if<std::is_floating_point<U>::value, std::string>::type
  valueToString(const U &val) const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << val;
    return oss.str();
  }

  // Helper to convert value to string for display - bool
  template <typename U = T>
  typename std::enable_if<std::is_same<U, bool>::value, std::string>::type
  valueToString(const U &val) const {
    return val ? "true" : "false";
  }

  // Helper to convert value to string for display - string
  template <typename U = T>
  typename std::enable_if<std::is_same<U, std::string>::value,
                          std::string>::type
  valueToString(const U &val) const {
    return val;
  }

  // Fallback for other types (including std::vector)
  template <typename U = T>
  typename std::enable_if<
      !std::is_integral<U>::value && !std::is_floating_point<U>::value &&
          !std::is_same<U, bool>::value && !std::is_same<U, std::string>::value,
      std::string>::type
  valueToString(const U &val) const {
    // For vectors and other complex types, just show a placeholder
    return "[complex type]";
  }

  // Notify all observers (widgets) - MUST be called with lock held
  void notifyObserversLocked() {
    // ================================================================
    // TEXT OBSERVERS
    // ================================================================
    std::string newText = valueToString(value);

    // Clean up expired text observers
    observers.erase(std::remove_if(observers.begin(), observers.end(),
                                   [](const std::weak_ptr<Widget> &w) {
                                     return w.expired();
                                   }),
                    observers.end());

    // Update all text observer widgets
    for (auto &weakWidget : observers) {
      if (auto widget = weakWidget.lock()) {
        widget->text = newText;

        if (ui) {
          ui->updateWidget(widget.get());
        }
      }
    }

    // ================================================================
    // PROPERTY BINDINGS
    // ================================================================

    // Clean up expired property bindings
    propertyBindings.erase(std::remove_if(propertyBindings.begin(),
                                          propertyBindings.end(),
                                          [](const PropertyBinding &b) {
                                            return b.widget.expired();
                                          }),
                           propertyBindings.end());

    // Fire each binding - surgically mutate only the bound property
    for (auto &binding : propertyBindings) {
      if (auto widget = binding.widget.lock()) {
        // Mutate ONLY the specific property, nothing else
        binding.applier(widget.get(), value);

        if (ui) {
          if (binding.needsLayout)
            ui->partialRebuild(widget.get()); // font size, padding etc
          else
            ui->invalidateWidget(widget.get()); // color, border etc
        }
      }
    }
  }

  // Notify all listeners (callbacks) - MUST be called with lock held
  void notifyListenersLocked(const T &newValue) {
    // Make a copy of listeners to avoid issues if a listener modifies the list
    auto listenersCopy = listeners;

    // Release lock before calling listeners to avoid deadlocks
    stateMutex.unlock();

    for (auto &listener : listenersCopy) {
      if (listener) {
        try {
          listener(newValue);
        } catch (const std::exception &e) {
// Log error but continue notifying other listeners
#ifdef FLUX_DEBUG
          std::cerr << "[FluxUI] Listener error: " << e.what() << std::endl;
#endif
        }
      }
    }

    // Re-acquire lock for caller
    stateMutex.lock();
  }

public:
  // ========================================================================
  // CONSTRUCTORS
  // ========================================================================

  // Primary constructor - ALWAYS require explicit FluxUI context
  State(T initial, FluxUI *app) : value(initial), ui(app) {
    assert(ui != nullptr && "State requires valid FluxUI context");
  }

  // Constructor for use within Component - gets context from Component
  State(T initial) : value(initial), ui(nullptr) {
    ui = FluxUI::getCurrentInstance();
#ifdef FLUX_DEBUG
    if (!ui) {
      std::cerr << "[FluxUI] Warning: State created without valid context"
                << std::endl;
    }
#endif
  }

  // Move constructor
  State(State &&other) noexcept {
    std::lock_guard<std::mutex> lock(other.stateMutex);
    value = std::move(other.value);
    ui = other.ui;
    observers = std::move(other.observers);
    listeners = std::move(other.listeners);
    other.ui = nullptr;
  }

  // Move assignment
  State &operator=(State &&other) noexcept {
    if (this != &other) {
      std::lock(stateMutex, other.stateMutex);
      std::lock_guard<std::mutex> lock1(stateMutex, std::adopt_lock);
      std::lock_guard<std::mutex> lock2(other.stateMutex, std::adopt_lock);

      value = std::move(other.value);
      ui = other.ui;
      observers = std::move(other.observers);
      listeners = std::move(other.listeners);
      other.ui = nullptr;
    }
    return *this;
  }

  // Disable copy operations to prevent accidental copies
  State(const State &) = delete;
  State &operator=(const State &) = delete;

  // ========================================================================
  // CORE API - ONLY FOUR METHODS
  // ========================================================================

  /**
   * Get current value (thread-safe)
   *
   * Usage:
   *   int currentValue = counter.get();
   */
  T get() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return value;
  }

  // Register a property binding (called from widget builder methods)
  void bindProperty(std::shared_ptr<Widget> widget,
                    std::function<void(Widget *, const T &)> applier,
                    bool needsLayout = false) {
    if (!widget)
      return;

    std::lock_guard<std::mutex> lock(stateMutex);

    propertyBindings.push_back({widget, applier, needsLayout});

    // Apply immediately - widget starts with correct value
    applier(widget.get(), value);
  }

  /**
   * Set new value (thread-safe)
   * Only triggers update if value actually changes
   *
   * Usage:
   *   counter.set(42);
   *   counter.set(counter.get() + 1);
   */
  void set(T newValue) {
    std::lock_guard<std::mutex> lock(stateMutex);

    if (value == newValue)
      return;

    value = newValue;

    notifyObserversLocked();
    notifyListenersLocked(value);
  }

  /**
   * Update with function (Flutter-like setState) - thread-safe
   * Useful for updates that depend on current value
   *
   * Usage:
   *   counter.update([](int v) { return v + 1; });
   *   toggle.update([](bool v) { return !v; });
   *   name.update([](std::string s) { return s + "!"; });
   */
  void update(std::function<T(T)> updater) {
    std::lock_guard<std::mutex> lock(stateMutex);

    T newValue = updater(value);

    if (value == newValue)
      return;

    value = newValue;

    notifyObserversLocked();
    notifyListenersLocked(value);
  }

  /**
   * Add a change listener (thread-safe)
   * Callback is invoked whenever the state changes
   *
   * Usage:
   *   counter.listen([](int newValue) {
   *       std::cout << "Counter changed to: " << newValue << std::endl;
   *   });
   */
  void listen(std::function<void(T)> listener) {
    if (!listener)
      return;

    std::lock_guard<std::mutex> lock(stateMutex);
    listeners.push_back(listener);
  }

  // ========================================================================
  // INTERNAL OBSERVER MANAGEMENT (for Widget binding)
  // ========================================================================

  // Add a widget observer (thread-safe)
  void addObserver(std::shared_ptr<Widget> widget) {
    if (!widget)
      return;

    std::lock_guard<std::mutex> lock(stateMutex);
    observers.push_back(widget);
    widget->boundState = this;

    // Set initial text
    widget->text = valueToString(value);
  }

  // Remove a widget observer (thread-safe)
  void removeObserver(Widget *widget) {
    if (!widget)
      return;

    std::lock_guard<std::mutex> lock(stateMutex);
    observers.erase(std::remove_if(observers.begin(), observers.end(),
                                   [widget](const std::weak_ptr<Widget> &w) {
                                     if (auto locked = w.lock()) {
                                       return locked.get() == widget;
                                     }
                                     return true; // Remove expired pointers too
                                   }),
                    observers.end());
  }

  // Clean up expired observers (thread-safe)
  void cleanupExpiredObservers() {
    std::lock_guard<std::mutex> lock(stateMutex);
    observers.erase(std::remove_if(observers.begin(), observers.end(),
                                   [](const std::weak_ptr<Widget> &w) {
                                     return w.expired();
                                   }),
                    observers.end());
  }

  // ========================================================================
  // UTILITY METHODS
  // ========================================================================

  // Convert to string (thread-safe)
  std::string toString() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return valueToString(value);
  }

  // Check if state has a valid UI context
  bool hasContext() const { return ui != nullptr; }

  // Get the UI context
  FluxUI *getContext() const { return ui; }

  // Get number of observers (thread-safe)
  size_t observerCount() const {
    std::lock_guard<std::mutex> lock(stateMutex);

    // Count only non-expired observers
    size_t count = 0;
    for (const auto &weak : observers) {
      if (!weak.expired())
        count++;
    }
    return count;
  }

  // Get number of listeners (thread-safe)
  size_t listenerCount() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return listeners.size();
  }

  // Clear all listeners (thread-safe)
  void clearListeners() {
    std::lock_guard<std::mutex> lock(stateMutex);
    listeners.clear();
  }
};

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// Create a computed state that depends on other states
template <typename R, typename... States> class ComputedState {
private:
  std::function<R(States...)> computer;
  std::tuple<States *...> dependencies;
  mutable R cachedValue;
  mutable bool dirty = true;
  mutable std::mutex mutex;

public:
  ComputedState(std::function<R(States...)> comp, States &...deps)
      : computer(comp), dependencies(&deps...) {
    // Add listeners to all dependencies
    addListenersImpl(std::index_sequence_for<States...>{});
  }

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
    auto markDirty = [this](auto) {
      std::lock_guard<std::mutex> lock(mutex);
      dirty = true;
    };

    (std::get<Is>(dependencies)->listen(markDirty), ...);
  }

  template <size_t... Is> R computeImpl(std::index_sequence<Is...>) const {
    return computer(std::get<Is>(dependencies)->get()...);
  }
};

#endif // FLUX_STATE_HPP