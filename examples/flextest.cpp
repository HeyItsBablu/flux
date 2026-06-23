#include "flux/flux.hpp"

struct Todo
{
    int64_t id;
    std::string text;
};

class MyApp : public Widget
{

    State<std::vector<Todo>> todos{{{1, "Buy groceries"},
                                    {2, "Finish homework"},
                                    {3, "Read a book"},
                                    {4, "Go for a walk"},
                                    {5, "Call mom"},
                                    {6, "Write some code"},
                                    {7, "Clean the room"},
                                    {8, "Pay the bills"},
                                    {9, "Exercise for 30 minutes"},
                                    {10, "Plan the weekend"}}};

    // std::vector<Todo> todos = {{1, "Buy groceries"},
    //                            {2, "Finish homework"},
    //                            {3, "Read a book"},
    //                            {4, "Go for a walk"},
    //                            {5, "Call mom"},
    //                            {6, "Write some code"},
    //                            {7, "Clean the room"},
    //                            {8, "Pay the bills"},
    //                            {9, "Exercise for 30 minutes"},
    //                            {10, "Plan the weekend"}};

public:
    WidgetPtr build() override
    {
        return Flex({FlexBuilder(todos, [](int, const Todo &t)
                                 { return FlexItemKey::fromInt64(t.id); }, [](int, const Todo &t)
                                 { return Text(t.text); })
                         ->setDirection(FlexDirection::Column)
                         ->setWidth(600)
                         ->setHeight(300)
                         ->setScrollable(true),
                     Button("Delete Item", [this]
                            { todos.erase(2); })

                    })

            ->setBackgroundColor(Color::fromRGB(280, 180, 180))
            ->setScrollable(false)
            ->setDirection(FlexDirection::Column) 
            ->setGap(8)
            ->setPadding(16)
            ->setAlignItems(AlignItems::Stretch)
            ->setWidthMode(SizeMode::Full)
            ->setHeightMode(SizeMode::Full);
    }
};

WidgetPtr createApp(FluxUI *app)
{
    return FluxApp("FluxUI - App", std::make_shared<MyApp>(), AppTheme::light(),
                   false, 900, 700, false, false);
}