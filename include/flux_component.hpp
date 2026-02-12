#ifndef FLUX_COMPONENTS_HPP
#define FLUX_COMPONENTS_HPP


#include "flux_state.hpp"
#include <cassert>
#include <memory>
#include <vector>

// ============================================================================
// COMPONENT BASE CLASS
// ============================================================================

/**
 * @brief Base class for all FluxUI components (Flutter-inspired).
 * 
 * Components encapsulate UI logic and state management. They provide a
 * declarative way to build widget trees with lifecycle methods.
 * 
 * Lifecycle:
 * 1. Constructor - Initialize component with FluxUI context
 * 2. initState() - Called once before first build (optional)
 * 3. build() - Called to create widget tree (required)
 * 4. rebuild() - Called when state changes
 * 5. dispose() - Called before destruction (optional cleanup)
 * 
 * @note Components hold a weak reference to the FluxUI context to prevent
 *       circular dependencies and allow proper cleanup.
 */
class Component
{
protected:
    FluxUI *context;  // FluxUI context (not owned)
    bool initialized = false;
    bool disposed = false;

public:
    /**
     * @brief Construct a new Component
     * @param ctx FluxUI context (must not be null)
     */
    Component(FluxUI *ctx) : context(ctx)
    {
        assert(ctx != nullptr && "Component requires valid FluxUI context");
    }

    virtual ~Component()
    {
        if (!disposed)
        {
            dispose();
        }
    }

    // Disable copy/move to prevent issues with state management
    Component(const Component &) = delete;
    Component &operator=(const Component &) = delete;
    Component(Component &&) = delete;
    Component &operator=(Component &&) = delete;

    /**
     * @brief Build the widget tree for this component.
     * @return WidgetPtr The root widget of this component's tree
     * 
     * This method is called whenever the component needs to render.
     * It should be pure (no side effects) and return a consistent
     * widget tree for the same state.
     */
    virtual WidgetPtr build() = 0;

    /**
     * @brief Initialize component state (called once before first build).
     * 
     * Override this to set up listeners, timers, or other initialization
     * that should happen only once. This is called automatically before
     * the first build().
     */
    virtual void initState() {}

    /**
     * @brief Cleanup resources before component is destroyed.
     * 
     * Override this to remove listeners, cancel timers, or perform
     * other cleanup. Called automatically in destructor if not already called.
     */
    virtual void dispose()
    {
        disposed = true;
    }

    /**
     * @brief Create a reactive state bound to this component.
     * @tparam T Type of the state value
     * @param initialValue Initial value for the state
     * @return State<T> A new state object
     * 
     * States created this way are automatically bound to the component's
     * FluxUI context and will trigger UI updates when changed.
     * 
     * @example
     * auto counter = useState(0);
     * counter.set(5);  // Triggers rebuild
     */
    template <typename T>
    State<T> useState(T initialValue)
    {
        assert(context != nullptr && "Component context is null");
        return State<T>(initialValue, context);
    }

    /**
     * @brief Request a rebuild of this component's UI.
     * 
     * This triggers the build() method to be called again and the
     * UI to be updated. Usually called automatically when state changes,
     * but can be manually triggered if needed.
     */
    void rebuild()
    {
        if (disposed)
        {
            #ifdef FLUX_DEBUG
            std::cerr << "[FluxUI] Warning: rebuild() called on disposed component" << std::endl;
            #endif
            return;
        }

        if (context)
        {
            context->rebuild();
        }
    }

    /**
     * @brief Check if component has been initialized.
     * @return true if initState() has been called
     */
    bool isInitialized() const { return initialized; }

    /**
     * @brief Check if component has been disposed.
     * @return true if dispose() has been called
     */
    bool isDisposed() const { return disposed; }

    /**
     * @brief Get the FluxUI context.
     * @return FluxUI* pointer to context (may be null if disposed)
     */
    FluxUI *getContext() const { return context; }

protected:
    /**
     * @brief Mark component as initialized.
     * Called automatically by ComponentBuilder.
     */
    void markInitialized()
    {
        initialized = true;
    }

    friend class ComponentBuilder;
};

// ============================================================================
// STATELESS COMPONENT
// ============================================================================

/**
 * @brief Component without internal state (Flutter-inspired).
 * 
 * StatelessComponents are simple components that only depend on their
 * constructor parameters and don't maintain internal state. They're
 * perfect for presentational widgets that just render data.
 * 
 * @example
 * class WelcomeText : public StatelessComponent {
 *     std::string name;
 * public:
 *     WelcomeText(FluxUI* ctx, const std::string& n) 
 *         : StatelessComponent(ctx), name(n) {}
 *     
 *     WidgetPtr build() override {
 *         return Text("Welcome, " + name);
 *     }
 * };
 */
class StatelessComponent : public Component
{
public:
    StatelessComponent(FluxUI *ctx) : Component(ctx) {}

    /**
     * @brief Build the widget tree.
     * @return WidgetPtr Root widget
     * 
     * Must be implemented by derived classes. Should be a pure function
     * that returns the same widget tree for the same input.
     */
    virtual WidgetPtr build() = 0;
};

// ============================================================================
// STATEFUL COMPONENT
// ============================================================================

/**
 * @brief Component with internal state and lifecycle (Flutter-inspired).
 * 
 * StatefulComponents maintain internal state and have a full lifecycle
 * with initState() and dispose() methods. Use these for interactive
 * components that change over time.
 * 
 * @example
 * class Counter : public StatefulComponent {
 *     State<int> count;
 * public:
 *     Counter(FluxUI* ctx) : StatefulComponent(ctx), count(0, ctx) {}
 *     
 *     void initState() override {
 *         count.listen([this](int v) {
 *             std::cout << "Count: " << v << std::endl;
 *         });
 *     }
 *     
 *     WidgetPtr build() override {
 *         return Button("Count: " + count.toString(), [this]() {
 *             count++;
 *         });
 *     }
 *     
 *     void dispose() override {
 *         count.clearListeners();
 *         StatefulComponent::dispose();
 *     }
 * };
 */
class StatefulComponent : public Component
{
public:
    StatefulComponent(FluxUI *ctx) : Component(ctx) {}

    /**
     * @brief Initialize state before first build.
     * 
     * Override this to set up listeners, initialize state, or perform
     * other one-time setup. Called automatically before the first build().
     */
    virtual void initState() override {}

    /**
     * @brief Build the widget tree.
     * @return WidgetPtr Root widget
     * 
     * Must be implemented by derived classes. Called whenever the
     * component needs to render (initially and on state changes).
     */
    virtual WidgetPtr build() = 0;

    /**
     * @brief Cleanup before destruction.
     * 
     * Override this to remove listeners, free resources, etc.
     * Remember to call the base class dispose() at the end.
     */
    virtual void dispose() override
    {
        Component::dispose();
    }
};

// ============================================================================
// COMPONENT BUILDER HELPER
// ============================================================================

/**
 * @brief Helper class to properly initialize and build components.
 * 
 * Ensures that initState() is called before build() and handles
 * the component lifecycle correctly.
 */
class ComponentBuilder
{
public:
    /**
     * @brief Build a component with proper lifecycle management.
     * @tparam TComponent Component type
     * @tparam Args Constructor argument types
     * @param args Constructor arguments (FluxUI* must be first)
     * @return WidgetPtr The built widget tree
     * 
     * @example
     * auto widget = ComponentBuilder::build<MyComponent>(&app, arg1, arg2);
     */
    template <typename TComponent, typename... Args>
    static WidgetPtr build(Args &&...args)
    {
        static_assert(std::is_base_of<Component, TComponent>::value,
                      "TComponent must derive from Component");

        // Create component
        auto component = std::make_unique<TComponent>(std::forward<Args>(args)...);

        // Initialize if not already done
        if (!component->isInitialized())
        {
            try
            {
                component->initState();
                component->markInitialized();
            }
            catch (const std::exception &e)
            {
                #ifdef FLUX_DEBUG
                std::cerr << "[FluxUI] Component initState() failed: " << e.what() << std::endl;
                #endif
                throw;
            }
        }

        // Build the widget tree
        try
        {
            return component->build();
        }
        catch (const std::exception &e)
        {
            #ifdef FLUX_DEBUG
            std::cerr << "[FluxUI] Component build() failed: " << e.what() << std::endl;
            #endif
            throw;
        }
    }

    /**
     * @brief Create and manage a long-lived component.
     * @tparam TComponent Component type
     * @tparam Args Constructor argument types
     * @param args Constructor arguments
     * @return std::shared_ptr<TComponent> Managed component
     * 
     * Use this when you need to keep the component alive and call
     * build() multiple times (e.g., for rebuild on state changes).
     */
    template <typename TComponent, typename... Args>
    static std::shared_ptr<TComponent> create(Args &&...args)
    {
        static_assert(std::is_base_of<Component, TComponent>::value,
                      "TComponent must derive from Component");

        auto component = std::make_shared<TComponent>(std::forward<Args>(args)...);

        if (!component->isInitialized())
        {
            try
            {
                component->initState();
                component->markInitialized();
            }
            catch (const std::exception &e)
            {
                #ifdef FLUX_DEBUG
                std::cerr << "[FluxUI] Component initState() failed: " << e.what() << std::endl;
                #endif
                throw;
            }
        }

        return component;
    }
};

// ============================================================================
// COMPONENT UTILITIES
// ============================================================================

/**
 * @brief Base class for components that manage multiple child components.
 * 
 * Automatically handles disposal of child components.
 */
class CompositeComponent : public StatefulComponent
{
protected:
    std::vector<std::shared_ptr<Component>> children;

public:
    CompositeComponent(FluxUI *ctx) : StatefulComponent(ctx) {}

    /**
     * @brief Add a child component.
     * @param child Component to add
     */
    void addComponent(std::shared_ptr<Component> child)
    {
        if (child)
        {
            children.push_back(child);
        }
    }

    /**
     * @brief Dispose all child components.
     */
    void dispose() override
    {
        for (auto &child : children)
        {
            if (child && !child->isDisposed())
            {
                child->dispose();
            }
        }
        children.clear();
        StatefulComponent::dispose();
    }
};

// ============================================================================
// CONVENIENCE MACROS
// ============================================================================

/**
 * @brief Create a component and immediately build it.
 * 
 * @example
 * FLUX_BUILD_COMPONENT(MyComponent, &app, arg1, arg2)
 */
#define FLUX_BUILD_COMPONENT(ComponentType, ...) \
    ComponentBuilder::build<ComponentType>(__VA_ARGS__)

/**
 * @brief Create a managed component instance.
 * 
 * @example
 * auto myComp = FLUX_CREATE_COMPONENT(MyComponent, &app, arg1, arg2);
 * auto widget = myComp->build();
 */
#define FLUX_CREATE_COMPONENT(ComponentType, ...) \
    ComponentBuilder::create<ComponentType>(__VA_ARGS__)

#endif // FLUX_COMPONENTS_HPP