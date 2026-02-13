#include "flux.hpp"
#include <windows.h>
#include <iostream>

class LayoutStateTestApp : public StatefulComponent
{
private:
    State<int> leftCount;
    State<int> centerCount;
    State<int> rightCount;
    State<int> totalCount;
    State<std::string> winner;
    State<bool> showDetails;

public:
    LayoutStateTestApp()
        : leftCount(0, context),
          centerCount(0, context),
          rightCount(0, context),
          totalCount(0, context),
          winner("No winner yet", context),
          showDetails(true, context)
    {
        // Test complex listen() chains
        leftCount.listen([this](int val) {
            std::cout << "[Left] Changed to: " << val << std::endl;
            updateTotal();
            updateWinner();
        });

        centerCount.listen([this](int val) {
            std::cout << "[Center] Changed to: " << val << std::endl;
            updateTotal();
            updateWinner();
        });

        rightCount.listen([this](int val) {
            std::cout << "[Right] Changed to: " << val << std::endl;
            updateTotal();
            updateWinner();
        });

        showDetails.listen([](bool show) {
            std::cout << "[Details] " << (show ? "Shown" : "Hidden") << std::endl;
        });
    }

    void updateTotal()
    {
        int total = leftCount.get() + centerCount.get() + rightCount.get();
        totalCount.set(total);
    }

    void updateWinner()
    {
        int left = leftCount.get();
        int center = centerCount.get();
        int right = rightCount.get();

        if (left > center && left > right)
            winner.set("Left is winning! 🏆");
        else if (center > left && center > right)
            winner.set("Center is winning! 🏆");
        else if (right > left && right > center)
            winner.set("Right is winning! 🏆");
        else if (left == center && center == right && left > 0)
            winner.set("It's a tie!");
        else
            winner.set("No winner yet");
    }

    void incrementLeft()
    {
        leftCount.update([](int v) { return v + 1; });
    }

    void incrementCenter()
    {
        centerCount.update([](int v) { return v + 1; });
    }

    void incrementRight()
    {
        rightCount.update([](int v) { return v + 1; });
    }

    void resetAll()
    {
        leftCount.set(0);
        centerCount.set(0);
        rightCount.set(0);
    }

    void toggleDetails()
    {
        showDetails.update([](bool v) { return !v; });
    }

    WidgetPtr build() override
    {
        auto theme = ThemeProvider::getTheme();

        // Determine colors based on winner
COLORREF leftColor   = RGB(0, 120, 255);    // Bright Blue
COLORREF centerColor = RGB(138, 43, 226);   // Purple
COLORREF rightColor  = RGB(255, 20, 147);   // Deep Pink

        std::string winnerText = winner.get();
        if (winnerText.find("Left") != std::string::npos)
            leftColor = RGB(255, 215, 0);
        else if (winnerText.find("Center") != std::string::npos)
            centerColor = RGB(255, 215, 0);
        else if (winnerText.find("Right") != std::string::npos)
            rightColor = RGB(255, 215, 0);

        return Scaffold(
            ThemedAppBar("Layout & State Test"),
            
            Column(
                // Test: Expanded with Center layout fix
                Expanded(
                    Center(
                        Padding(20,
                            Column(
                                // Winner banner
                                ThemedCard(
                                    Column(
                                        Text(winner)
                                            ->setFontSize(24)
                                            ->setFontWeight(FontWeight::Bold)
                                            ->setTextColor(theme.primaryColor),
                                        
                                        SizedBox(0, 10),
                                        
                                        Row(
                                            Text("Total Clicks:")
                                                ->setFontSize(16),
                                            SizedBox(10, 0),
                                            Text(totalCount)
                                                ->setFontSize(32)
                                                ->setFontWeight(FontWeight::Bold)
                                                ->setTextColor(theme.primaryColor)
                                        )
                                        ->setMainAxisAlignment(MainAxisAlignment::Center)
                                        ->setCrossAlignment(Alignment::Center)
                                    )
                                    ->setSpacing(0)
                                    ->setCrossAlignment(Alignment::Center)
                                )->setMinWidth(500),

                                SizedBox(0, 30),

                                // Three columns with counters (tests Row layout)
                                Row(
                                    // Left counter
                                    ThemedCard(
                                        Column(
                                            Text("Left")
                                                ->setFontSize(14)
                                                ->setTextColor(theme.secondaryTextColor),
                                            
                                            SizedBox(0, 10),
                                            
                                            Text(leftCount)
                                                ->setFontSize(48)
                                                ->setFontWeight(FontWeight::Bold)
                                                ->setTextColor(leftColor),
                                            
                                            SizedBox(0, 15),
                                            
                                            ThemedButton("+1", [this]() { incrementLeft(); })
                                        )
                                        ->setSpacing(0)
                                        ->setCrossAlignment(Alignment::Center)
                                    )->setMinWidth(180),

                                    // Center counter
                                    ThemedCard(
                                        Column(
                                            Text("Center")
                                                ->setFontSize(14)
                                                ->setTextColor(theme.secondaryTextColor),
                                            
                                            SizedBox(0, 10),
                                            
                                            Text(centerCount)
                                                ->setFontSize(48)
                                                ->setFontWeight(FontWeight::Bold)
                                                ->setTextColor(centerColor),
                                            
                                            SizedBox(0, 15),
                                            
                                            ThemedButton("+1", [this]() { incrementCenter(); })
                                        )
                                        ->setSpacing(0)
                                        ->setCrossAlignment(Alignment::Center)
                                    )->setMinWidth(180),

                                    // Right counter
                                    ThemedCard(
                                        Column(
                                            Text("Right")
                                                ->setFontSize(14)
                                                ->setTextColor(theme.secondaryTextColor),
                                            
                                            SizedBox(0, 10),
                                            
                                            Text(rightCount)
                                                ->setFontSize(48)
                                                ->setFontWeight(FontWeight::Bold)
                                                ->setTextColor(rightColor),
                                            
                                            SizedBox(0, 15),
                                            
                                            ThemedButton("+1", [this]() { incrementRight(); })->setBackgroundColor(rightColor)
                                        )
                                        ->setSpacing(0)
                                        ->setCrossAlignment(Alignment::Center)
                                    )->setMinWidth(180)
                                )
                                ->setSpacing(20)
                                ->setMainAxisAlignment(MainAxisAlignment::Center),

                                SizedBox(0, 30),

                                // Controls
                                Row(
                                    ThemedButton("Reset All", [this]() { resetAll(); }),
                                    ThemedButton(
                                        showDetails.get() ? "Hide Details" : "Show Details",
                                        [this]() { toggleDetails(); }
                                    )
                                )
                                ->setSpacing(15)
                                ->setMainAxisAlignment(MainAxisAlignment::Center),

                                // Conditional rendering based on state
                                showDetails.get() 
                                    ? Column(
                                        SizedBox(0, 20),
                                        ThemedCard(
                                            Column(
                                                Text("Details Panel")
                                                    ->setFontSize(16)
                                                    ->setFontWeight(FontWeight::Bold),
                                                
                                                Divider(),
                                                
                                                SizedBox(0, 10),
                                                
                                                Text("Left: " + std::to_string(leftCount.get()))
                                                    ->setFontSize(14),
                                                Text("Center: " + std::to_string(centerCount.get()))
                                                    ->setFontSize(14),
                                                Text("Right: " + std::to_string(rightCount.get()))
                                                    ->setFontSize(14),
                                                
                                                SizedBox(0, 10),
                                                
                                                Text("Total: " + std::to_string(totalCount.get()))
                                                    ->setFontSize(14)
                                                    ->setFontWeight(FontWeight::Bold)
                                            )
                                            ->setSpacing(5)
                                            ->setCrossAlignment(Alignment::Start)
                                        )->setMinWidth(400)
                                    )->setSpacing(0)
                                    : SizedBox(0, 0)  // Empty when hidden
                            )
                            ->setSpacing(0)
                            ->setCrossAlignment(Alignment::Center)
                        )
                    )
                )
            )
            ->setSpacing(0)
        );
    }
};

WidgetPtr layoutStateTestApp(FluxUI *app)
{
    return FluxApp(
        "Layout & State Test",
        BuildComponent<LayoutStateTestApp>(),
        AppTheme::materialBlue()
    );
}
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    AllocConsole();
    FILE *fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);

    std::cout << "=== FluxUI State API & Layout Tests ===" << std::endl;
    std::cout << "Choose test:" << std::endl;
    std::cout << "1. Counter Test (State API: get, set, update, listen)" << std::endl;
    std::cout << "2. Layout Test (Expanded + Center fix + Reactive state)" << std::endl;
    
    FluxUI app(hInstance);
    
    // Test 1: State API operations
    // app.build([&]() { return counterTestApp(&app); });
    
    // Test 2: Layout + State integration
    app.build([&]() { return layoutStateTestApp(&app); });
    
    app.createWindow("FluxUI Test Suite", 1000, 700);
    
    return app.run();
}