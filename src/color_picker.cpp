#include "flux.hpp"
#include <windows.h>
#include <iostream>

class StatCard : public StatefulComponent
{
private:
    std::string label;
    State<int> value;
    COLORREF color;

public:
    StatCard(const std::string &lbl, COLORREF col)
        : label(lbl), value(0, context), color(col)
    {
        // Log changes
        value.listen([lbl](int newVal) {
            std::cout << lbl << " updated to: " << newVal << std::endl;
        });
    }

    void increment()
    {
        value.update([](int v) { return v + 1; });
    }

    void decrement()
    {
        if (value.get() > 0)
        {
            value.update([](int v) { return v - 1; });
        }
    }

    void addTen()
    {
        value.update([](int v) { return v + 10; });
    }

    void reset()
    {
        value.set(0);
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

                Text(value)
                    ->setFontSize(48)
                    ->setFontWeight(FontWeight::Bold)
                    ->setTextColor(color),
                    
                SizedBox(0, 15),
                
                Column(
                    ThemedButton("+1", [this]() { increment(); }),
                    ThemedButton("+10", [this]() { addTen(); }),
                    ThemedButton("-1", [this]() { decrement(); }),
                    ThemedButton("Reset", [this]() { reset(); })
                )
                ->setSpacing(5)
            )
            ->setSpacing(0)
            ->setCrossAlignment(Alignment::Center)
        )->setMinWidth(180);
    }
};

class Dashboard : public StatefulComponent
{
private:
    State<int> totalClicks;
    State<std::string> summary;

public:
    Dashboard()
        : totalClicks(0, context),
          summary("No activity yet", context)
    {
        // Update summary whenever total changes
        totalClicks.listen([this](int total) {
            if (total == 0)
            {
                summary.set("No activity yet");
            }
            else if (total < 10)
            {
                summary.set("Just getting started...");
            }
            else if (total < 50)
            {
                summary.set("Making progress!");
            }
            else
            {
                summary.set("You're on fire! 🔥");
            }
        });
    }

    void recordClick()
    {
        totalClicks.update([](int v) { return v + 1; });
    }

    void resetAll()
    {
        totalClicks.set(0);
    }

    WidgetPtr build() override
    {
        auto theme = ThemeProvider::getTheme();

        return Scaffold(
            ThemedAppBar("Analytics Dashboard"),

            Padding(20,
                Column(
                    Text("Performance Metrics")
                        ->setFontSize(theme.titleFontSize)
                        ->setFontWeight(theme.titleFontWeight)
                        ->setTextColor(theme.textColor),

                    SizedBox(0, 10),
                    
                    Text(summary)
                        ->setFontSize(16)
                        ->setTextColor(theme.secondaryTextColor),

                    SizedBox(0, 20),

                    Row(
                        BuildComponent<StatCard>("Sales", RGB(34, 139, 34)),
                        BuildComponent<StatCard>("Leads", RGB(30, 144, 255)),
                        BuildComponent<StatCard>("Revenue", RGB(255, 140, 0))
                    )
                    ->setSpacing(15)
                    ->setMainAxisAlignment(MainAxisAlignment::Center),
                    
                    SizedBox(0, 30),
                    
                    ThemedCard(
                        Column(
                            Text("Total Activity")
                                ->setFontSize(14),
                            Text(totalClicks)
                                ->setFontSize(32)
                                ->setFontWeight(FontWeight::Bold)
                                ->setTextColor(theme.primaryColor)
                        )
                        ->setSpacing(5)
                        ->setCrossAlignment(Alignment::Center)
                    ),
                    
                    SizedBox(0, 20),
                    
                    ThemedButton("Reset Dashboard", [this]() { resetAll(); })
                )
                ->setSpacing(0)
            )
        );
    }
};

WidgetPtr dashboardApp(FluxUI *app)
{
    return FluxApp(
        "Analytics Dashboard",
        BuildComponent<Dashboard>(),
        AppTheme::materialBlue()
    );
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    AllocConsole();
    FILE *fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);

    std::cout << "Starting FluxUI Dashboard..." << std::endl;
    
    FluxUI app(hInstance);
    app.build([&]() { return dashboardApp(&app); });
    app.createWindow("FluxUI Demo", 900, 650);
    
    return app.run();
}