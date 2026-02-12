#include "flux.hpp"
#include <windows.h>
#include <iostream>

class ChildWidget : public StatefulComponent
{
private:
    std::string label;
    State<int> counter;

public:
    ChildWidget(const std::string &lbl)
        : label(lbl), counter(0, context) {}

    void updateVariable()
    {
        counter.set(counter.get() + 1);
    }

    WidgetPtr build() override
    {
        auto theme = ThemeProvider::getTheme();

        return ThemedCard(
                   Column(
                       Text(label)
                           ->setFontSize(14)
                           ->setTextColor(theme.secondaryTextColor),

                       SizedBox(0, 10),

                       Text(counter)
                           ->setFontSize(32)
                           ->setFontWeight(FontWeight::Bold),
                       SizedBox(0, 10),
                       ThemedButton("Add", [this]()
                                    { updateVariable(); }))
                       ->setSpacing(0)
                       ->setCrossAlignment(Alignment::Center))
            ->setMinWidth(150);
    }
};

class ThemedDashboard : public StatefulComponent
{
    State<int> total;
    State<int> active;
    State<int> completed;

public:
    ThemedDashboard()
        : total(0, context),
          active(0, context),
          completed(0, context) {}

    WidgetPtr build() override
    {
        auto theme = ThemeProvider::getTheme();

        return Scaffold(
            ThemedAppBar("Dashboard"),

            Padding(20,
                    Column(
                        Text("Task Statistics")
                            ->setFontSize(theme.titleFontSize)
                            ->setFontWeight(theme.titleFontWeight)
                            ->setTextColor(theme.textColor),

                        SizedBox(0, 20),

                        Row(
                            BuildComponent<ChildWidget>("Hello"),
                            BuildComponent<ChildWidget>("New"),
                            BuildComponent<ChildWidget>("World"))
                            ->setSpacing(15)
                            ->setMainAxisAlignment(MainAxisAlignment::Center))
                        ->setSpacing(0)));
    }
};

WidgetPtr dashboardApp(FluxUI *app)
{
    return FluxApp(
        "Themed Dashboard",
        BuildComponent<ThemedDashboard>(),
        AppTheme::materialBlue());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{

    // Enable console for debugging
    AllocConsole();
    FILE *fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);

    std::cout << "Starting FluxUI..." << std::endl;
    FluxUI app(hInstance);

    // Complex examples:
    app.build([&]()
              { return dashboardApp(&app); });

    app.createWindow("FluxApp Demo", 800, 600);
    return app.run();
}