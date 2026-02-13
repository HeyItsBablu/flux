#ifndef FLUX_LISTVIEW_HPP
#define FLUX_LISTVIEW_HPP

#include "flux_core.hpp"
#include "flux_state.hpp"
#include "flux_widget_list.hpp"
#include <functional>
#include <vector>

// ============================================================================
// LISTVIEW BUILDER WIDGET
// ============================================================================

/**
 * ListViewBuilder: Dynamic list with builder pattern (like Flutter)
 * 
 * Usage:
 *   State<std::vector<std::string>> items({"Item 1", "Item 2"}, &app);
 *   
 *   ListView(items)
 *       ->itemBuilder([](int index, const std::string& item) {
 *           return Card(Text(item));
 *       })
 *       ->separator([]() { return Divider(); })
 *       ->spacing(8)
 */
template<typename T>
class ListViewBuilder : public Widget
{
private:
    State<std::vector<T>>* boundState = nullptr;
    std::function<WidgetPtr(int, const T&)> builder;
    std::function<WidgetPtr()> separatorBuilder;
    int itemSpacing = 0;
    int lastItemCount = 0;
    std::shared_ptr<ListViewBuilder<T>> self;

    void rebuildList()
    {
        if (!boundState || !builder)
            return;

        const auto& items = boundState->get();
        
        // Only rebuild if count changed (optimization)
        if (items.size() == lastItemCount && !children.empty())
            return;

        lastItemCount = items.size();

        // Clear old children
        children.clear();

        // Build new items
        for (size_t i = 0; i < items.size(); i++)
        {
            auto itemWidget = builder(i, items[i]);
            if (itemWidget)
            {
                addChild(itemWidget);
            }

            // Add separator if not last item
            if (separatorBuilder && i < items.size() - 1)
            {
                auto separator = separatorBuilder();
                if (separator)
                {
                    addChild(separator);
                }
            }
        }

        markNeedsLayout();
    }

public:
    ListViewBuilder(State<std::vector<T>>& state) : boundState(&state)
    {
        lastItemCount = state.get().size();

        // Listen for state changes
        state.listen([this](const std::vector<T>& newValue) {
            rebuildList();
            
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

    void setSelf(std::shared_ptr<ListViewBuilder<T>> ptr)
    {
        self = ptr;
    }

    /**
     * Set the item builder function
     * 
     * @param builderFunc Function that takes (index, item) and returns a widget
     * 
     * @example
     * ListView(items)->itemBuilder([](int i, const Item& item) {
     *     return Text(item.name);
     * })
     */
    std::shared_ptr<ListViewBuilder<T>> itemBuilder(std::function<WidgetPtr(int, const T&)> builderFunc)
    {
        builder = builderFunc;
        rebuildList(); // Initial build
        return self;
    }

    /**
     * Set separator between items
     * 
     * @example
     * ListView(items)->separator([]() { return Divider(); })
     */
    std::shared_ptr<ListViewBuilder<T>> separator(std::function<WidgetPtr()> separatorFunc)
    {
        separatorBuilder = separatorFunc;
        return self;
    }

    /**
     * Set spacing between items (in pixels)
     */
    std::shared_ptr<ListViewBuilder<T>> spacing(int space)
    {
        itemSpacing = space;
        return self;
    }

    void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache& fontCache) override
    {
        rebuildList();

        int contentWidth = availableWidth - paddingLeft - paddingRight;
        int contentHeight = availableHeight - paddingTop - paddingBottom;
        int totalHeight = 0;
        int maxWidth = 0;

        // Layout all children
        for (size_t i = 0; i < children.size(); i++)
        {
            auto& child = children[i];
            child->computeLayout(hdc, contentWidth, contentHeight, fontCache);
            
            totalHeight += child->height;
            if (child->width > maxWidth)
                maxWidth = child->width;
            
            // Add spacing between items (but not after separators)
            if (itemSpacing > 0 && i < children.size() - 1)
            {
                totalHeight += itemSpacing;
            }
        }

        if (autoWidth)
            width = maxWidth + paddingLeft + paddingRight;
        if (autoHeight)
            height = totalHeight + paddingTop + paddingBottom;

        applyConstraints();
        needsLayout = false;
    }

    void positionChildren(int contentX, int contentY, int contentWidth, int contentHeight) override
    {
        int currentY = contentY;

        for (size_t i = 0; i < children.size(); i++)
        {
            auto& child = children[i];
            
            child->x = contentX;
            child->y = currentY;

            child->positionChildren(
                child->x + child->paddingLeft,
                child->y + child->paddingTop,
                child->width - child->paddingLeft - child->paddingRight,
                child->height - child->paddingTop - child->paddingBottom
            );

            currentY += child->height;
            
            if (itemSpacing > 0 && i < children.size() - 1)
            {
                currentY += itemSpacing;
            }
        }
    }
};

// ============================================================================
// SIMPLE LISTVIEW (NON-BUILDER)
// ============================================================================

/**
 * SimpleListView: List with static children
 */
class SimpleListView : public Widget
{
private:
    int itemSpacing = 0;
    std::shared_ptr<SimpleListView> self;

public:
    SimpleListView() = default;

    void setSelf(std::shared_ptr<SimpleListView> ptr)
    {
        self = ptr;
    }

    std::shared_ptr<SimpleListView> spacing(int space)
    {
        itemSpacing = space;
        return self;
    }

    void computeLayout(HDC hdc, int availableWidth, int availableHeight, FontCache& fontCache) override
    {
        int contentWidth = availableWidth - paddingLeft - paddingRight;
        int contentHeight = availableHeight - paddingTop - paddingBottom;
        int totalHeight = 0;
        int maxWidth = 0;

        for (size_t i = 0; i < children.size(); i++)
        {
            auto& child = children[i];
            child->computeLayout(hdc, contentWidth, contentHeight, fontCache);
            
            totalHeight += child->height;
            if (child->width > maxWidth)
                maxWidth = child->width;
            
            if (i < children.size() - 1)
            {
                totalHeight += itemSpacing;
            }
        }

        if (autoWidth)
            width = maxWidth + paddingLeft + paddingRight;
        if (autoHeight)
            height = totalHeight + paddingTop + paddingBottom;

        applyConstraints();
        needsLayout = false;
    }

    void positionChildren(int contentX, int contentY, int contentWidth, int contentHeight) override
    {
        int currentY = contentY;

        for (size_t i = 0; i < children.size(); i++)
        {
            auto& child = children[i];
            
            child->x = contentX;
            child->y = currentY;

            child->positionChildren(
                child->x + child->paddingLeft,
                child->y + child->paddingTop,
                child->width - child->paddingLeft - child->paddingRight,
                child->height - child->paddingTop - child->paddingBottom
            );

            currentY += child->height + itemSpacing;
        }
    }
};

// ============================================================================
// FACTORY FUNCTIONS
// ============================================================================

/**
 * Create a ListView.builder widget
 * 
 * @param state State containing vector of items
 * @return ListViewBuilder<T>* Chainable list widget
 * 
 * @example
 * State<std::vector<Task>> tasks({...}, &app);
 * 
 * ListView(tasks)
 *   ->itemBuilder([](int index, const Task& task) {
 *       return Card(
 *           Column(
 *               Text(task.title)->setFontWeight(FontWeight::Bold),
 *               Text(task.description)
 *           )
 *       );
 *   })
 *   ->separator([]() { return Divider(); })
 *   ->spacing(8)
 */
template<typename T>
inline std::shared_ptr<ListViewBuilder<T>> ListView(State<std::vector<T>>& state)
{
    auto widget = std::make_shared<ListViewBuilder<T>>(state);
    widget->setSelf(widget);
    return widget;
}

/**
 * Create a simple ListView with static children
 * 
 * @example
 * ListViewStatic(
 *     Text("Item 1"),
 *     Text("Item 2"),
 *     Text("Item 3")
 * )->spacing(8)
 */
template<typename... Widgets>
inline std::shared_ptr<SimpleListView> ListViewStatic(Widgets... widgets)
{
    auto w = std::make_shared<SimpleListView>();
    w->setSelf(w);
    (w->addChild(widgets), ...);
    return w;
}

// ============================================================================
// USAGE EXAMPLES
// ============================================================================

/*

EXAMPLE 1: Basic string list
-----------------------------

State<std::vector<std::string>> names({
    "Alice", "Bob", "Charlie", "David"
}, &app);

auto list = ListView(names)
    ->itemBuilder([](int index, const std::string& name) {
        return Text(name);
    })
    ->spacing(4);


EXAMPLE 2: List with separators
--------------------------------

State<std::vector<std::string>> items({"A", "B", "C"}, &app);

ListView(items)
    ->itemBuilder([](int i, const std::string& item) {
        return Card(Text(item));
    })
    ->separator([]() { 
        return Divider(); 
    });


EXAMPLE 3: Complex item widgets
--------------------------------

struct Task {
    std::string title;
    std::string description;
    bool completed;
};

State<std::vector<Task>> tasks({...}, &app);

ListView(tasks)
    ->itemBuilder([&](int index, const Task& task) {
        return Card(
            Row(
                Column(
                    Text(task.title)
                        ->setFontWeight(FontWeight::Bold),
                    Text(task.description)
                        ->setTextColor(RGB(100, 100, 100))
                )->setFlex(1),
                
                Button(task.completed ? "✓" : "○", [&, index]() {
                    tasks.update([index](std::vector<Task> t) {
                        t[index].completed = !t[index].completed;
                        return t;
                    });
                })
            )
        );
    })
    ->spacing(8);


EXAMPLE 4: Dynamic list with add/remove
---------------------------------------

State<std::vector<std::string>> items({"Item 1"}, &app);

Column(
    // Add button
    Button("Add Item", [&]() {
        items.update([](std::vector<std::string> v) {
            v.push_back("Item " + std::to_string(v.size() + 1));
            return v;
        });
    }),
    
    // List
    Expanded(
        ListView(items)
            ->itemBuilder([&](int index, const std::string& item) {
                return Row(
                    Text(item)->setFlex(1),
                    Button("Remove", [&, index]() {
                        items.update([index](std::vector<std::string> v) {
                            v.erase(v.begin() + index);
                            return v;
                        });
                    })
                );
            })
            ->spacing(4)
    )
);


EXAMPLE 5: Index-based styling
-------------------------------

ListView(items)
    ->itemBuilder([](int index, const std::string& item) {
        auto widget = Card(Text(item));
        
        // Alternate colors
        if (index % 2 == 0) {
            widget->setBackgroundColor(RGB(240, 240, 240));
        } else {
            widget->setBackgroundColor(RGB(255, 255, 255));
        }
        
        return widget;
    })
    ->spacing(2);


EXAMPLE 6: Numbers/Counters
---------------------------

State<std::vector<int>> numbers({1, 2, 3, 4, 5}, &app);

ListView(numbers)
    ->itemBuilder([&](int index, int number) {
        return Row(
            Text(std::to_string(number))->setFlex(1),
            Button("+", [&, index]() {
                numbers.update([index](std::vector<int> v) {
                    v[index]++;
                    return v;
                });
            }),
            Button("-", [&, index]() {
                numbers.update([index](std::vector<int> v) {
                    v[index]--;
                    return v;
                });
            })
        );
    })
    ->spacing(4);


EXAMPLE 7: Static ListView (no builder)
---------------------------------------

ListViewStatic(
    Card(Text("First Item")),
    Card(Text("Second Item")),
    Card(Text("Third Item"))
)->spacing(8);


EXAMPLE 8: Empty state handling
--------------------------------

State<std::vector<Item>> items({}, &app);

Switch(itemsCount)
    ->Case(0, []() {
        return Center(
            Text("No items yet")
                ->setTextColor(RGB(150, 150, 150))
        );
    })
    ->Default([&]() {
        return ListView(items)
            ->itemBuilder([](int i, const Item& item) {
                return ItemWidget(item);
            })
            ->spacing(8);
    });

*/

#endif // FLUX_LISTVIEW_HPP