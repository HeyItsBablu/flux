#ifndef FLUX_SWITCH_HPP
#define FLUX_SWITCH_HPP

#include "flux_state.hpp"
#include "flux_widget_list.hpp"
#include <functional>
#include <map>

// ============================================================================
// SWITCH-CASE WIDGET - C++ STYLE
// ============================================================================

/**
 * SwitchWidget: C++-style switch-case conditional rendering
 * 
 * Usage:
 *   State<int> page(0, &app);
 *   
 *   Switch(page)
 *       ->Case(0, []() { return Text("Home"); })
 *       ->Case(1, []() { return Text("Profile"); })
 *       ->Case(2, []() { return Text("Settings"); })
 *       ->Default([]() { return Text("Unknown Page"); })
 */
template<typename T>
class SwitchWidget : public Widget
{
private:
    State<T>* boundState = nullptr;
    std::map<T, std::function<WidgetPtr()>> cases;
    std::function<WidgetPtr()> defaultCase;
    WidgetPtr currentChild = nullptr;
    T lastValue;
    std::shared_ptr<SwitchWidget<T>> self; // Store self reference

    void rebuildChild()
    {
        if (!boundState)
            return;

        T currentValue = boundState->get();
        
        // Only rebuild if value changed
        if (currentChild && currentValue == lastValue)
            return;

        lastValue = currentValue;

        // Clear old child
        children.clear();
        currentChild = nullptr;

        // Find matching case
        auto it = cases.find(currentValue);
        if (it != cases.end() && it->second)
        {
            currentChild = it->second();
        }
        else if (defaultCase)
        {
            currentChild = defaultCase();
        }

        if (currentChild)
        {
            addChild(currentChild);
        }

        markNeedsLayout();
    }

public:
    SwitchWidget(State<T>& state) : boundState(&state)
    {
        lastValue = state.get();

        // Listen for state changes
        state.listen([this](T newValue) {
            rebuildChild();
            
            if (boundState && boundState->hasContext())
            {
                auto* ui = boundState->getContext();
                if (ui)
                {
                    ui->partialRebuild(this);
                }
            }
        });
    }

    // Store the shared_ptr to self for chaining
    void setSelf(std::shared_ptr<SwitchWidget<T>> ptr)
    {
        self = ptr;
    }

    /**
     * Add a case
     * 
     * @example
     * Switch(state)
     *   ->Case(0, []() { return Text("Zero"); })
     *   ->Case(1, []() { return Text("One"); })
     */
    std::shared_ptr<SwitchWidget<T>> Case(T value, std::function<WidgetPtr()> builder)
    {
        cases[value] = builder;
        return self;
    }

    /**
     * Add default case (like default: in C++)
     * 
     * @example
     * Switch(state)
     *   ->Case(0, builder0)
     *   ->Default([]() { return Text("Unknown"); })
     */
    std::shared_ptr<SwitchWidget<T>> Default(std::function<WidgetPtr()> builder)
    {
        defaultCase = builder;
        return self;
    }

    void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache& fontCache) override
    {
        rebuildChild();

        if (!children.empty())
        {
            children[0]->computeLayout(hdc, 
                availableWidth - paddingLeft - paddingRight,
                availableHeight - paddingTop - paddingBottom, 
                fontCache);
            
            if (autoWidth)
                width = children[0]->width + paddingLeft + paddingRight;
            if (autoHeight)
                height = children[0]->height + paddingTop + paddingBottom;
        }

        applyConstraints();
        needsLayout = false;
    }

    void positionChildren(int contentX, int contentY, int contentWidth, int contentHeight) override
    {
        if (!children.empty())
        {
            auto& child = children[0];
            child->x = contentX;
            child->y = contentY;

            child->positionChildren(
                child->x + child->paddingLeft,
                child->y + child->paddingTop,
                child->width - child->paddingLeft - child->paddingRight,
                child->height - child->paddingTop - child->paddingBottom
            );
        }
    }
};

// ============================================================================
// FACTORY FUNCTION
// ============================================================================

/**
 * Create a switch-case widget (C++ style)
 * 
 * @param state State to switch on
 * @return std::shared_ptr<SwitchWidget<T>> Chainable widget pointer
 * 
 * @example
 * State<int> tabIndex(0, &app);
 * 
 * Switch(tabIndex)
 *   ->Case(0, []() { return HomePage(); })
 *   ->Case(1, []() { return ProfilePage(); })
 *   ->Case(2, []() { return SettingsPage(); })
 *   ->Default([]() { return ErrorPage(); })
 */
template<typename T>
inline std::shared_ptr<SwitchWidget<T>> Switch(State<T>& state)
{
    auto widget = std::make_shared<SwitchWidget<T>>(state);
    widget->setSelf(widget); // Store self reference for chaining
    return widget;
}



#endif // FLUX_SWITCH_HPP