#ifndef FLUX_STATE_HPP
#define FLUX_STATE_HPP

#include <algorithm>
#include <cassert>
#include <functional>
#include <iomanip>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <type_traits>
#include <vector>

// ============================================================================
// SFINAE TYPE-TRAIT DETECTORS
// ============================================================================


namespace flux_detail {

template <typename T, typename = void>
struct has_value_type : std::false_type {};
template <typename T>
struct has_value_type<T, std::void_t<typename T::value_type>> : std::true_type {};

template <typename T, typename = void>
struct has_mapped_type : std::false_type {};
template <typename T>
struct has_mapped_type<T, std::void_t<typename T::mapped_type>> : std::true_type {};

// Safe alias — resolves to void when the member type is absent.
template <typename T, typename = void>
struct value_type_of   { using type = void; };
template <typename T>
struct value_type_of<T, std::void_t<typename T::value_type>> {
    using type = typename T::value_type;
};

template <typename T, typename = void>
struct key_type_of     { using type = void; };
template <typename T>
struct key_type_of<T, std::void_t<typename T::key_type>> {
    using type = typename T::key_type;
};

template <typename T, typename = void>
struct mapped_type_of  { using type = void; };
template <typename T>
struct mapped_type_of<T, std::void_t<typename T::mapped_type>> {
    using type = typename T::mapped_type;
};

template <typename T>
inline constexpr bool has_value_type_v  = has_value_type<T>::value;
template <typename T>
inline constexpr bool has_mapped_type_v = has_mapped_type<T>::value;

// A "sequence" container has value_type but NOT mapped_type.
// This correctly excludes std::map / std::unordered_map while keeping
// std::vector, std::list, std::deque, etc.
template <typename T>
inline constexpr bool is_sequence_v =
    has_value_type_v<T> && !has_mapped_type_v<T>;

} // namespace flux_detail

// ---------------------------------------------------------------------------
// Local enable_if shorthand macros (undefined at end of file)
// ---------------------------------------------------------------------------
#define FLUX_IF_ARITH(T) \
    std::enable_if_t<std::is_arithmetic_v<T>, int> = 0
#define FLUX_IF_BOOL(T) \
    std::enable_if_t<std::is_same_v<T, bool>, int> = 0
#define FLUX_IF_STRING(T) \
    std::enable_if_t<std::is_same_v<T, std::string>, int> = 0
#define FLUX_IF_SEQ(T) \
    std::enable_if_t<flux_detail::is_sequence_v<T>, int> = 0
#define FLUX_IF_MAP(T) \
    std::enable_if_t<flux_detail::has_mapped_type_v<T>, int> = 0

// ============================================================================
// STATE CLASS
// ============================================================================

class FluxUI;

template <typename T>
class State {
private:
    T       value;
    FluxUI *ui;

    std::vector<std::weak_ptr<Widget>>       observers;
    std::vector<std::function<void(T)>>      listeners;

    struct PropertyBinding {
        std::weak_ptr<Widget>                    widget;
        std::function<void(Widget*, const T&)>   applier;
        bool                                     needsLayout;
    };
    std::vector<PropertyBinding> propertyBindings;

    mutable std::mutex stateMutex;

    // ── valueToString overloads ──────────────────────────────────────────

    template <typename U = T>
    std::enable_if_t<std::is_integral_v<U> && !std::is_same_v<U, bool>,
                     std::string>
    valueToString(const U &val) const { return std::to_string(val); }

    template <typename U = T>
    std::enable_if_t<std::is_floating_point_v<U>, std::string>
    valueToString(const U &val) const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << val;
        return oss.str();
    }

    template <typename U = T>
    std::enable_if_t<std::is_same_v<U, bool>, std::string>
    valueToString(const U &val) const { return val ? "true" : "false"; }

    template <typename U = T>
    std::enable_if_t<std::is_same_v<U, std::string>, std::string>
    valueToString(const U &val) const { return val; }

    template <typename U = T>
    std::enable_if_t<!std::is_integral_v<U>        &&
                     !std::is_floating_point_v<U>  &&
                     !std::is_same_v<U, bool>      &&
                     !std::is_same_v<U, std::string>,
                     std::string>
    valueToString(const U &) const { return "[complex type]"; }

    // ── Internal notification ────────────────────────────────────────────

    void notifyObserversLocked() {
        std::string newText = valueToString(value);

        observers.erase(
            std::remove_if(observers.begin(), observers.end(),
                [](const std::weak_ptr<Widget> &w){ return w.expired(); }),
            observers.end());

        for (auto &weakWidget : observers)
            if (auto widget = weakWidget.lock()) {
                widget->text = newText;
                if (ui) ui->updateWidget(widget.get());
            }

        propertyBindings.erase(
            std::remove_if(propertyBindings.begin(), propertyBindings.end(),
                [](const PropertyBinding &b){ return b.widget.expired(); }),
            propertyBindings.end());

        for (auto &binding : propertyBindings)
            if (auto widget = binding.widget.lock()) {
                binding.applier(widget.get(), value);
                if (ui) {
                    if (binding.needsLayout) ui->partialRebuild(widget.get());
                    else                     ui->invalidateWidget(widget.get());
                }
            }
    }

    std::vector<std::function<void(T)>> snapshotListeners() const {
        return listeners;
    }

    void dispatchListeners(const std::vector<std::function<void(T)>> &snap,
                           const T &val) {
        for (const auto &fn : snap) {
            if (!fn) continue;
            try { fn(val); }
            catch (const std::exception &e) {
#ifdef FLUX_DEBUG
                std::cerr << "[FluxUI] Listener error: " << e.what() << "\n";
#endif
            } catch (...) {
#ifdef FLUX_DEBUG
                std::cerr << "[FluxUI] Listener threw unknown exception\n";
#endif
            }
        }
    }

    // Mutex must NOT be held by caller.
    void setAndNotify(T newValue) {
        std::vector<std::function<void(T)>> snap;
        T dispatchValue;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            value         = std::move(newValue);
            dispatchValue = value;
            notifyObserversLocked();
            snap = snapshotListeners();
        }
        dispatchListeners(snap, dispatchValue);
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
            std::cerr << "[FluxUI] Warning: State created without valid context\n";
#endif
    }

    State(State &&other) noexcept {
        std::lock_guard<std::mutex> lock(other.stateMutex);
        value            = std::move(other.value);
        ui               = other.ui;
        observers        = std::move(other.observers);
        listeners        = std::move(other.listeners);
        propertyBindings = std::move(other.propertyBindings);
        other.ui         = nullptr;
    }

    State &operator=(State &&other) noexcept {
        if (this != &other) {
            std::lock(stateMutex, other.stateMutex);
            std::lock_guard<std::mutex> l1(stateMutex,       std::adopt_lock);
            std::lock_guard<std::mutex> l2(other.stateMutex, std::adopt_lock);
            value            = std::move(other.value);
            ui               = other.ui;
            observers        = std::move(other.observers);
            listeners        = std::move(other.listeners);
            propertyBindings = std::move(other.propertyBindings);
            other.ui         = nullptr;
        }
        return *this;
    }

    State(const State &)            = delete;
    State &operator=(const State &) = delete;

    // ========================================================================
    // CORE API
    // ========================================================================

    T get() const {
        std::lock_guard<std::mutex> lock(stateMutex);
        return value;
    }

    void set(T newValue) { setAndNotify(std::move(newValue)); }

    void update(std::function<T(T)> updater) {
        std::vector<std::function<void(T)>> snap;
        T dispatchValue;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            T newValue = updater(value);
            if (value == newValue) return;
            value         = std::move(newValue);
            dispatchValue = value;
            notifyObserversLocked();
            snap = snapshotListeners();
        }
        dispatchListeners(snap, dispatchValue);
    }

    /**
     * mutate() — modify the value in-place, always notifies.
     * Use when operator== won't detect a deep container change.
     *
     *   items.mutate([](auto& v){ v.push_back("x"); });
     */
    void mutate(std::function<void(T&)> mutator) {
        std::vector<std::function<void(T)>> snap;
        T dispatchValue;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            mutator(value);
            dispatchValue = value;
            notifyObserversLocked();
            snap = snapshotListeners();
        }
        dispatchListeners(snap, dispatchValue);
    }

    void listen(std::function<void(T)> listener) {
        if (!listener) return;
        std::lock_guard<std::mutex> lock(stateMutex);
        listeners.push_back(std::move(listener));
    }

    void bindProperty(std::shared_ptr<Widget>                  widget,
                      std::function<void(Widget*, const T&)>   applier,
                      bool needsLayout = false) {
        if (!widget) return;
        std::lock_guard<std::mutex> lock(stateMutex);
        propertyBindings.push_back({widget, std::move(applier), needsLayout});
        propertyBindings.back().applier(widget.get(), value);
    }

    // ========================================================================
    // OBSERVER MANAGEMENT
    // ========================================================================

    void addObserver(std::shared_ptr<Widget> widget) {
        if (!widget) return;
        std::lock_guard<std::mutex> lock(stateMutex);
        observers.push_back(widget);
        widget->text = valueToString(value);
    }

    void removeObserver(Widget *widget) {
        if (!widget) return;
        std::lock_guard<std::mutex> lock(stateMutex);
        observers.erase(
            std::remove_if(observers.begin(), observers.end(),
                [widget](const std::weak_ptr<Widget> &w) {
                    if (auto l = w.lock()) return l.get() == widget;
                    return true;
                }),
            observers.end());
    }

    void cleanupExpiredObservers() {
        std::lock_guard<std::mutex> lock(stateMutex);
        observers.erase(
            std::remove_if(observers.begin(), observers.end(),
                [](const std::weak_ptr<Widget> &w){ return w.expired(); }),
            observers.end());
    }

    // ========================================================================
    // UTILITY
    // ========================================================================

    std::string toString() const {
        std::lock_guard<std::mutex> lock(stateMutex);
        return valueToString(value);
    }

    bool    hasContext() const  { return ui != nullptr; }
    FluxUI *getContext() const  { return ui; }

    size_t observerCount() const {
        std::lock_guard<std::mutex> lock(stateMutex);
        size_t n = 0;
        for (const auto &w : observers) if (!w.expired()) ++n;
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

    // ========================================================================
    // ARITHMETIC OPERATORS  — numeric T only
    // ========================================================================

    template <typename U = T, FLUX_IF_ARITH(U)>
    T operator++() {
        T next;
        { std::lock_guard<std::mutex> lk(stateMutex); next = ++value; notifyObserversLocked(); }
        dispatchListeners(snapshotListeners(), next);
        return next;
    }

    template <typename U = T, FLUX_IF_ARITH(U)>
    T operator++(int) {
        T prev, cur;
        { std::lock_guard<std::mutex> lk(stateMutex); prev = value++; cur = value; notifyObserversLocked(); }
        dispatchListeners(snapshotListeners(), cur);
        return prev;
    }

    template <typename U = T, FLUX_IF_ARITH(U)>
    T operator--() {
        T next;
        { std::lock_guard<std::mutex> lk(stateMutex); next = --value; notifyObserversLocked(); }
        dispatchListeners(snapshotListeners(), next);
        return next;
    }

    template <typename U = T, FLUX_IF_ARITH(U)>
    T operator--(int) {
        T prev, cur;
        { std::lock_guard<std::mutex> lk(stateMutex); prev = value--; cur = value; notifyObserversLocked(); }
        dispatchListeners(snapshotListeners(), cur);
        return prev;
    }

    template <typename Rhs, typename U = T, FLUX_IF_ARITH(U)>
    State &operator+=(const Rhs &rhs) { setAndNotify(get() + static_cast<T>(rhs)); return *this; }

    template <typename Rhs, typename U = T, FLUX_IF_ARITH(U)>
    State &operator-=(const Rhs &rhs) { setAndNotify(get() - static_cast<T>(rhs)); return *this; }

    template <typename Rhs, typename U = T, FLUX_IF_ARITH(U)>
    State &operator*=(const Rhs &rhs) { setAndNotify(get() * static_cast<T>(rhs)); return *this; }

    template <typename Rhs, typename U = T, FLUX_IF_ARITH(U)>
    State &operator/=(const Rhs &rhs) { setAndNotify(get() / static_cast<T>(rhs)); return *this; }

    template <typename Rhs, typename U = T,
              std::enable_if_t<std::is_integral_v<U> && !std::is_same_v<U, bool>, int> = 0>
    State &operator%=(const Rhs &rhs) { setAndNotify(get() % static_cast<T>(rhs)); return *this; }

    // ── Clamp helpers ────────────────────────────────────────────────────

    template <typename U = T, FLUX_IF_ARITH(U)>
    void clamp(T lo, T hi) { update([lo,hi](T v){ return std::max(lo, std::min(hi, v)); }); }

    template <typename U = T, FLUX_IF_ARITH(U)>
    void clampMin(T lo) { update([lo](T v){ return std::max(lo, v); }); }

    template <typename U = T, FLUX_IF_ARITH(U)>
    void clampMax(T hi) { update([hi](T v){ return std::min(hi, v); }); }

    // ========================================================================
    // BOOLEAN HELPERS
    // ========================================================================

    template <typename U = T, FLUX_IF_BOOL(U)>
    void toggle() { update([](bool v){ return !v; }); }

    // ========================================================================
    // STRING HELPERS
    // ========================================================================

    template <typename U = T, FLUX_IF_STRING(U)>
    State &append(const std::string &suffix) {
        mutate([&](std::string &s){ s += suffix; });
        return *this;
    }

    template <typename U = T, FLUX_IF_STRING(U)>
    State &prepend(const std::string &prefix) {
        mutate([&](std::string &s){ s = prefix + s; });
        return *this;
    }

    template <typename U = T, FLUX_IF_STRING(U)>
    void clear() { set(std::string{}); }

    // ========================================================================
    // SEQUENCE CONTAINER HELPERS  (vector / list / deque — NOT map)
    // ========================================================================

    template <typename U = T, FLUX_IF_SEQ(U)>
    void push_back(typename flux_detail::value_type_of<U>::type item) {
        mutate([&](T &v){ v.push_back(std::move(item)); });
    }

    template <typename U = T, FLUX_IF_SEQ(U)>
    void push_front(typename flux_detail::value_type_of<U>::type item) {
        mutate([&](T &v){ v.insert(v.begin(), std::move(item)); });
    }

    template <typename U = T, FLUX_IF_SEQ(U)>
    std::optional<typename flux_detail::value_type_of<U>::type> pop_back() {
        std::optional<typename flux_detail::value_type_of<U>::type> removed;
        mutate([&](T &v){
            if (!v.empty()) { removed = std::move(v.back()); v.pop_back(); }
        });
        return removed;
    }

    template <typename U = T, FLUX_IF_SEQ(U)>
    std::optional<typename flux_detail::value_type_of<U>::type> pop_front() {
        std::optional<typename flux_detail::value_type_of<U>::type> removed;
        mutate([&](T &v){
            if (!v.empty()) { removed = std::move(v.front()); v.erase(v.begin()); }
        });
        return removed;
    }

    template <typename U = T, FLUX_IF_SEQ(U)>
    void insert(std::size_t index, typename flux_detail::value_type_of<U>::type item) {
        mutate([&](T &v){
            auto it = v.begin() + static_cast<std::ptrdiff_t>(std::min(index, v.size()));
            v.insert(it, std::move(item));
        });
    }

    template <typename U = T, FLUX_IF_SEQ(U)>
    void erase(std::size_t index) {
        mutate([&](T &v){
            if (index < v.size())
                v.erase(v.begin() + static_cast<std::ptrdiff_t>(index));
        });
    }

    template <typename U = T, FLUX_IF_SEQ(U)>
    void remove(const typename flux_detail::value_type_of<U>::type &item) {
        mutate([&](T &v){
            auto it = std::find(v.begin(), v.end(), item);
            if (it != v.end()) v.erase(it);
        });
    }

    template <typename U = T, typename Pred, FLUX_IF_SEQ(U)>
    void remove_if(Pred &&pred) {
        mutate([&](T &v){
            v.erase(std::remove_if(v.begin(), v.end(), std::forward<Pred>(pred)),
                    v.end());
        });
    }

    template <typename U = T, FLUX_IF_SEQ(U)>
    void clear() { mutate([](T &v){ v.clear(); }); }

    template <typename U = T, FLUX_IF_SEQ(U)>
    void sort() { mutate([](T &v){ std::sort(v.begin(), v.end()); }); }

    template <typename U = T, typename Cmp, FLUX_IF_SEQ(U)>
    void sort(Cmp &&cmp) {
        mutate([&](T &v){ std::sort(v.begin(), v.end(), std::forward<Cmp>(cmp)); });
    }

    template <typename U = T, FLUX_IF_SEQ(U)>
    void reverse() { mutate([](T &v){ std::reverse(v.begin(), v.end()); }); }

    template <typename U = T, FLUX_IF_SEQ(U)>
    typename flux_detail::value_type_of<U>::type at(std::size_t index) const {
        std::lock_guard<std::mutex> lock(stateMutex);
        return value.at(index);
    }

    template <typename U = T, FLUX_IF_SEQ(U)>
    void set_at(std::size_t index, typename flux_detail::value_type_of<U>::type item) {
        mutate([&](T &v){ if (index < v.size()) v[index] = std::move(item); });
    }

    template <typename U = T, FLUX_IF_SEQ(U)>
    std::size_t size() const {
        std::lock_guard<std::mutex> lock(stateMutex);
        return value.size();
    }

    template <typename U = T, FLUX_IF_SEQ(U)>
    bool empty() const {
        std::lock_guard<std::mutex> lock(stateMutex);
        return value.empty();
    }

    template <typename U = T, FLUX_IF_SEQ(U)>
    bool contains(const typename flux_detail::value_type_of<U>::type &item) const {
        std::lock_guard<std::mutex> lock(stateMutex);
        return std::find(value.begin(), value.end(), item) != value.end();
    }

    template <typename U = T, typename Transform, FLUX_IF_SEQ(U)>
    void map_values(Transform &&transform) {
        mutate([&](T &v){ for (auto &e : v) e = transform(e); });
    }

    // ========================================================================
    // MAP / UNORDERED_MAP HELPERS
    // ========================================================================

    template <typename U = T, FLUX_IF_MAP(U)>
    void insert_or_assign(typename flux_detail::key_type_of<U>::type    key,
                          typename flux_detail::mapped_type_of<U>::type val) {
        mutate([&](T &m){ m.insert_or_assign(std::move(key), std::move(val)); });
    }

    template <typename U = T, FLUX_IF_MAP(U)>
    void erase_key(const typename flux_detail::key_type_of<U>::type &key) {
        mutate([&](T &m){ m.erase(key); });
    }

    template <typename U = T, FLUX_IF_MAP(U)>
    typename flux_detail::mapped_type_of<U>::type
    value_for(const typename flux_detail::key_type_of<U>::type   &key,
              typename flux_detail::mapped_type_of<U>::type  defaultVal = {}) const {
        std::lock_guard<std::mutex> lock(stateMutex);
        auto it = value.find(key);
        return it != value.end() ? it->second : defaultVal;
    }

    template <typename U = T, FLUX_IF_MAP(U)>
    bool contains_key(const typename flux_detail::key_type_of<U>::type &key) const {
        std::lock_guard<std::mutex> lock(stateMutex);
        return value.count(key) > 0;
    }
};

// ============================================================================
// NON-MEMBER OPERATORS
// ============================================================================

template <typename T, typename Rhs>
auto operator+(const State<T> &s, const Rhs &r) -> decltype(s.get() + r) { return s.get() + r; }
template <typename Lhs, typename T>
auto operator+(const Lhs &l, const State<T> &s) -> decltype(l + s.get()) { return l + s.get(); }
template <typename T, typename Rhs>
auto operator-(const State<T> &s, const Rhs &r) -> decltype(s.get() - r) { return s.get() - r; }
template <typename Lhs, typename T>
auto operator-(const Lhs &l, const State<T> &s) -> decltype(l - s.get()) { return l - s.get(); }
template <typename T, typename Rhs>
auto operator*(const State<T> &s, const Rhs &r) -> decltype(s.get() * r) { return s.get() * r; }
template <typename T, typename Rhs>
auto operator/(const State<T> &s, const Rhs &r) -> decltype(s.get() / r) { return s.get() / r; }

template <typename T, typename Rhs> bool operator==(const State<T> &s, const Rhs &r) { return s.get() == r; }
template <typename T, typename Rhs> bool operator!=(const State<T> &s, const Rhs &r) { return s.get() != r; }
template <typename T, typename Rhs> bool operator< (const State<T> &s, const Rhs &r) { return s.get() <  r; }
template <typename T, typename Rhs> bool operator<=(const State<T> &s, const Rhs &r) { return s.get() <= r; }
template <typename T, typename Rhs> bool operator> (const State<T> &s, const Rhs &r) { return s.get() >  r; }
template <typename T, typename Rhs> bool operator>=(const State<T> &s, const Rhs &r) { return s.get() >= r; }

// ============================================================================
// HELPERS
// ============================================================================

template <typename T> inline State<T> &deref(State<T> *state) { return *state; }

template <typename R, typename... States>
class ComputedState {
private:
    std::function<R(States...)>  computer;
    std::tuple<States*...>       dependencies;
    mutable R                    cachedValue{};
    mutable bool                 dirty = true;
    mutable std::mutex           mutex;
    std::shared_ptr<bool>        alive = std::make_shared<bool>(true);

public:
    ComputedState(std::function<R(States...)> comp, States&... deps)
        : computer(std::move(comp)), dependencies(&deps...) {
        addListenersImpl(std::index_sequence_for<States...>{});
    }
    ~ComputedState() { *alive = false; }
    ComputedState(const ComputedState&)            = delete;
    ComputedState& operator=(const ComputedState&) = delete;
    ComputedState(ComputedState&&)                 = delete;
    ComputedState& operator=(ComputedState&&)      = delete;

    R get() const {
        std::lock_guard<std::mutex> lock(mutex);
        if (dirty) { cachedValue = computeImpl(std::index_sequence_for<States...>{}); dirty = false; }
        return cachedValue;
    }

private:
    template <size_t... Is>
    void addListenersImpl(std::index_sequence<Is...>) {
        std::weak_ptr<bool> wa = alive;
        std::mutex *mtx = &mutex;
        bool *d = &dirty;
        auto markDirty = [wa, mtx, d](auto){
            if (auto a = wa.lock(); a && *a) { std::lock_guard<std::mutex> lk(*mtx); *d = true; }
        };
        (std::get<Is>(dependencies)->listen(markDirty), ...);
    }
    template <size_t... Is>
    R computeImpl(std::index_sequence<Is...>) const {
        return computer(std::get<Is>(dependencies)->get()...);
    }
};

// Clean up local macros
#undef FLUX_IF_ARITH
#undef FLUX_IF_BOOL
#undef FLUX_IF_STRING
#undef FLUX_IF_SEQ
#undef FLUX_IF_MAP

#endif // FLUX_STATE_HPP