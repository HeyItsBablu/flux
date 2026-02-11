#ifndef FLUX_STATE_HPP
#define FLUX_STATE_HPP

#include "flux_core.hpp"
#include <mutex>
#include <cassert>
#include <sstream>
#include <iomanip>

// ============================================================================
// STATE CLASS - REACTIVE STATE MANAGEMENT
// ============================================================================

template <typename T>
class State
{
private:
    T value;
    FluxUI *ui;
    std::vector<std::weak_ptr<Widget>> observers;
    std::vector<std::function<void(T)>> listeners;
    
    // Thread safety
    mutable std::mutex stateMutex;
    
    // Performance optimization: cache string representation
    mutable std::string cachedString;
    mutable bool stringDirty = true;

    // ========================================================================
    // PRIVATE HELPERS
    // ========================================================================

    // Helper to convert value to string for display (with caching)
    template <typename U = T>
    typename std::enable_if<std::is_integral<U>::value && !std::is_same<U, bool>::value, std::string>::type
    valueToString(const U &val) const
    {
        if (stringDirty)
        {
            cachedString = std::to_string(val);
            stringDirty = false;
        }
        return cachedString;
    }

    template <typename U = T>
    typename std::enable_if<std::is_floating_point<U>::value, std::string>::type
    valueToString(const U &val) const
    {
        if (stringDirty)
        {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2) << val;
            cachedString = oss.str();
            stringDirty = false;
        }
        return cachedString;
    }

    template <typename U = T>
    typename std::enable_if<std::is_same<U, bool>::value, std::string>::type
    valueToString(const U &val) const
    {
        if (stringDirty)
        {
            cachedString = val ? "true" : "false";
            stringDirty = false;
        }
        return cachedString;
    }

    template <typename U = T>
    typename std::enable_if<std::is_same<U, std::string>::value, std::string>::type
    valueToString(const U &val) const
    {
        return val; // No caching needed for strings
    }

    // Notify all observers (widgets) - MUST be called with lock held
    void notifyObserversLocked()
    {
        std::string newText = valueToString(value);

        // Clean up expired observers
        observers.erase(
            std::remove_if(observers.begin(), observers.end(),
                           [](const std::weak_ptr<Widget> &w)
                           { return w.expired(); }),
            observers.end());

        // Update all observer widgets
        for (auto &weakWidget : observers)
        {
            if (auto widget = weakWidget.lock())
            {
                widget->text = newText;

                if (ui)
                {
                    ui->updateWidget(widget.get());
                }
            }
        }
    }

    // Notify all listeners (callbacks) - MUST be called with lock held
    void notifyListenersLocked(const T &newValue)
    {
        // Make a copy of listeners to avoid issues if a listener modifies the list
        auto listenersCopy = listeners;
        
        // Release lock before calling listeners to avoid deadlocks
        stateMutex.unlock();
        
        for (auto &listener : listenersCopy)
        {
            if (listener)
            {
                try
                {
                    listener(newValue);
                }
                catch (const std::exception &e)
                {
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
    State(T initial, FluxUI *app) : value(initial), ui(app)
    {
        assert(ui != nullptr && "State requires valid FluxUI context");
    }

    // Constructor for use within Component - gets context from Component
    State(T initial) : value(initial), ui(nullptr)
    {
        ui = FluxUI::getCurrentInstance();
        #ifdef FLUX_DEBUG
        if (!ui)
        {
            std::cerr << "[FluxUI] Warning: State created without valid context" << std::endl;
        }
        #endif
    }

    // Move constructor
    State(State &&other) noexcept
    {
        std::lock_guard<std::mutex> lock(other.stateMutex);
        value = std::move(other.value);
        ui = other.ui;
        observers = std::move(other.observers);
        listeners = std::move(other.listeners);
        cachedString = std::move(other.cachedString);
        stringDirty = other.stringDirty;
        other.ui = nullptr;
    }

    // Move assignment
    State &operator=(State &&other) noexcept
    {
        if (this != &other)
        {
            std::lock(stateMutex, other.stateMutex);
            std::lock_guard<std::mutex> lock1(stateMutex, std::adopt_lock);
            std::lock_guard<std::mutex> lock2(other.stateMutex, std::adopt_lock);

            value = std::move(other.value);
            ui = other.ui;
            observers = std::move(other.observers);
            listeners = std::move(other.listeners);
            cachedString = std::move(other.cachedString);
            stringDirty = other.stringDirty;
            other.ui = nullptr;
        }
        return *this;
    }

    // Disable copy operations to prevent accidental copies
    State(const State &) = delete;
    State &operator=(const State &) = delete;

    // ========================================================================
    // GETTERS
    // ========================================================================

    // Get current value (thread-safe)
    T get() const
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        return value;
    }

    // Get reference to value (use with caution - returns copy for thread safety)
    T snapshot() const
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        return value;
    }

    // Convert to string (thread-safe)
    std::string toString() const
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        return valueToString(value);
    }

    // ========================================================================
    // SETTERS
    // ========================================================================

    // Set new value (thread-safe)
    void set(T newValue)
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        
        if (value == newValue)
            return;

        value = newValue;
        stringDirty = true; // Invalidate cached string

        notifyObserversLocked();
        notifyListenersLocked(value);
    }

    // Update with function (Flutter-like setState) - thread-safe
    void update(std::function<T(T)> updater)
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        
        T newValue = updater(value);
        
        if (value == newValue)
            return;

        value = newValue;
        stringDirty = true;

        notifyObserversLocked();
        notifyListenersLocked(value);
    }

    // Force update (even if value is the same) - thread-safe
    void forceUpdate()
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        stringDirty = true;
        notifyObserversLocked();
        notifyListenersLocked(value);
    }

    // ========================================================================
    // ARITHMETIC OPERATORS (for numeric types)
    // ========================================================================

    // Pre-increment: ++state
    template <typename U = T>
    typename std::enable_if<std::is_arithmetic<U>::value, State<T> &>::type
    operator++()
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        value = value + 1;
        stringDirty = true;
        notifyObserversLocked();
        notifyListenersLocked(value);
        return *this;
    }

    // Post-increment: state++
    template <typename U = T>
    typename std::enable_if<std::is_arithmetic<U>::value, T>::type
    operator++(int)
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        T oldValue = value;
        value = value + 1;
        stringDirty = true;
        notifyObserversLocked();
        notifyListenersLocked(value);
        return oldValue;
    }

    // Pre-decrement: --state
    template <typename U = T>
    typename std::enable_if<std::is_arithmetic<U>::value, State<T> &>::type
    operator--()
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        value = value - 1;
        stringDirty = true;
        notifyObserversLocked();
        notifyListenersLocked(value);
        return *this;
    }

    // Post-decrement: state--
    template <typename U = T>
    typename std::enable_if<std::is_arithmetic<U>::value, T>::type
    operator--(int)
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        T oldValue = value;
        value = value - 1;
        stringDirty = true;
        notifyObserversLocked();
        notifyListenersLocked(value);
        return oldValue;
    }

    // Compound assignment: state += 5
    template <typename U = T>
    typename std::enable_if<std::is_arithmetic<U>::value, State<T> &>::type
    operator+=(const T &val)
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        value = value + val;
        stringDirty = true;
        notifyObserversLocked();
        notifyListenersLocked(value);
        return *this;
    }

    // Compound assignment: state -= 5
    template <typename U = T>
    typename std::enable_if<std::is_arithmetic<U>::value, State<T> &>::type
    operator-=(const T &val)
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        value = value - val;
        stringDirty = true;
        notifyObserversLocked();
        notifyListenersLocked(value);
        return *this;
    }

    // Compound assignment: state *= 2
    template <typename U = T>
    typename std::enable_if<std::is_arithmetic<U>::value, State<T> &>::type
    operator*=(const T &val)
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        value = value * val;
        stringDirty = true;
        notifyObserversLocked();
        notifyListenersLocked(value);
        return *this;
    }

    // Compound assignment: state /= 2
    template <typename U = T>
    typename std::enable_if<std::is_arithmetic<U>::value, State<T> &>::type
    operator/=(const T &val)
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        if (val != 0)
        {
            value = value / val;
            stringDirty = true;
            notifyObserversLocked();
            notifyListenersLocked(value);
        }
        #ifdef FLUX_DEBUG
        else
        {
            std::cerr << "[FluxUI] Warning: Division by zero attempted" << std::endl;
        }
        #endif
        return *this;
    }

    // ========================================================================
    // COMPARISON OPERATORS (compare with value directly)
    // ========================================================================

    bool operator==(const T &other) const
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        return value == other;
    }

    bool operator!=(const T &other) const
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        return value != other;
    }

    template <typename U = T>
    typename std::enable_if<std::is_arithmetic<U>::value, bool>::type
    operator<(const T &other) const
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        return value < other;
    }

    template <typename U = T>
    typename std::enable_if<std::is_arithmetic<U>::value, bool>::type
    operator>(const T &other) const
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        return value > other;
    }

    template <typename U = T>
    typename std::enable_if<std::is_arithmetic<U>::value, bool>::type
    operator<=(const T &other) const
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        return value <= other;
    }

    template <typename U = T>
    typename std::enable_if<std::is_arithmetic<U>::value, bool>::type
    operator>=(const T &other) const
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        return value >= other;
    }

    // ========================================================================
    // BOOLEAN OPERATORS (for bool type)
    // ========================================================================

    // Toggle: state.toggle() for bool
    template <typename U = T>
    typename std::enable_if<std::is_same<U, bool>::value, void>::type
    toggle()
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        value = !value;
        stringDirty = true;
        notifyObserversLocked();
        notifyListenersLocked(value);
    }

    // Logical NOT: !state for bool
    template <typename U = T>
    typename std::enable_if<std::is_same<U, bool>::value, bool>::type
    operator!() const
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        return !value;
    }

    // ========================================================================
    // OBSERVER MANAGEMENT
    // ========================================================================

    // Add a widget observer (thread-safe)
    void addObserver(std::shared_ptr<Widget> widget)
    {
        if (!widget)
            return;

        std::lock_guard<std::mutex> lock(stateMutex);
        observers.push_back(widget);
        widget->boundState = this;
        
        // Set initial text
        widget->text = valueToString(value);
    }

    // Remove a widget observer (thread-safe)
    void removeObserver(Widget *widget)
    {
        if (!widget)
            return;

        std::lock_guard<std::mutex> lock(stateMutex);
        observers.erase(
            std::remove_if(observers.begin(), observers.end(),
                           [widget](const std::weak_ptr<Widget> &w)
                           {
                               if (auto locked = w.lock())
                               {
                                   return locked.get() == widget;
                               }
                               return true; // Remove expired pointers too
                           }),
            observers.end());
    }

    // Remove all observers (thread-safe)
    void clearObservers()
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        observers.clear();
    }

    // Clean up expired observers (thread-safe)
    void cleanupExpiredObservers()
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        observers.erase(
            std::remove_if(observers.begin(), observers.end(),
                           [](const std::weak_ptr<Widget> &w)
                           { return w.expired(); }),
            observers.end());
    }

    // ========================================================================
    // LISTENER MANAGEMENT (callbacks)
    // ========================================================================

    // Add a change listener (thread-safe)
    void addListener(std::function<void(T)> listener)
    {
        if (!listener)
            return;

        std::lock_guard<std::mutex> lock(stateMutex);
        listeners.push_back(listener);
    }

    // Convenience method (Flutter-like) - thread-safe
    void listen(std::function<void(T)> listener)
    {
        addListener(listener);
    }

    // Remove all listeners (thread-safe)
    void clearListeners()
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        listeners.clear();
    }

    // ========================================================================
    // UTILITY METHODS
    // ========================================================================

    // Check if state has a valid UI context
    bool hasContext() const
    {
        return ui != nullptr;
    }

    // Get the UI context
    FluxUI *getContext() const
    {
        return ui;
    }

    // Get number of observers (thread-safe)
    size_t observerCount() const
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        
        // Count only non-expired observers
        size_t count = 0;
        for (const auto &weak : observers)
        {
            if (!weak.expired())
                count++;
        }
        return count;
    }

    // Get number of listeners (thread-safe)
    size_t listenerCount() const
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        return listeners.size();
    }

    // ========================================================================
    // ADVANCED FEATURES
    // ========================================================================

    // Conditional update - only update if predicate is true
    void setIf(T newValue, std::function<bool(T, T)> predicate)
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        
        if (predicate(value, newValue))
        {
            value = newValue;
            stringDirty = true;
            notifyObserversLocked();
            notifyListenersLocked(value);
        }
    }

    // Batch update - update without notifications, then notify once
    template <typename Func>
    void batch(Func func)
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        
        T oldValue = value;
        func(*this);
        
        if (value != oldValue)
        {
            stringDirty = true;
            notifyObserversLocked();
            notifyListenersLocked(value);
        }
    }

    // Transform state value
    template <typename Func>
    void transform(Func transformer)
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        
        T newValue = transformer(value);
        
        if (value != newValue)
        {
            value = newValue;
            stringDirty = true;
            notifyObserversLocked();
            notifyListenersLocked(value);
        }
    }
};

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// Create a computed state that depends on other states
template <typename R, typename... States>
class ComputedState
{
private:
    std::function<R(States...)> computer;
    std::tuple<States*...> dependencies;
    mutable R cachedValue;
    mutable bool dirty = true;
    mutable std::mutex mutex;

public:
    ComputedState(std::function<R(States...)> comp, States&... deps)
        : computer(comp), dependencies(&deps...)
    {
        // Add listeners to all dependencies
        addListenersImpl(std::index_sequence_for<States...>{});
    }

    R get() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (dirty)
        {
            cachedValue = computeImpl(std::index_sequence_for<States...>{});
            dirty = false;
        }
        return cachedValue;
    }

private:
    template <size_t... Is>
    void addListenersImpl(std::index_sequence<Is...>)
    {
        auto markDirty = [this](auto) { 
            std::lock_guard<std::mutex> lock(mutex);
            dirty = true; 
        };
        
        (std::get<Is>(dependencies)->addListener(markDirty), ...);
    }

    template <size_t... Is>
    R computeImpl(std::index_sequence<Is...>) const
    {
        return computer(std::get<Is>(dependencies)->get()...);
    }
};

#endif // FLUX_STATE_HPP