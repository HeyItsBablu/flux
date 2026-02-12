#include "flux.hpp"
#include <windows.h>

// Custom reusable widget - Stat Card
WidgetPtr StatCard(const std::string& label, State<int>& value, COLORREF color) {
    return Card(
        Column(
            Text(label)
                ->setFontSize(14)
                ->setTextColor(RGB(120, 120, 120)),
            
            Text(value)
                ->setFontSize(36)
                ->setFontWeight(FontWeight::Bold)
                ->setTextColor(color)
        )
        ->setSpacing(8)
    )
    ->setMinWidth(150)
    ->setMinHeight(100);
}

// Main app using the custom widget
WidgetPtr simpleApp(FluxUI* app) {
    static State<int> likes(42, app);
    static State<int> views(1500, app);
    
    return Scaffold(

        Center(
            Column(
                // Use custom StatCard widgets
                Row(
                    StatCard("Likes", likes, RGB(244, 67, 54)),
                    StatCard("Views", views, RGB(33, 150, 243))
                )
                ->setSpacing(20),
                
                SizedBox(0, 20),
                
                // Control buttons
                Row(
                    Button("+ Like", [&]() { likes++; }),
                    Button("+ View", [&]() { views++; })
                )
                ->setSpacing(10)
            )
            ->setSpacing(0)
        )
    );
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    FluxUI app(hInstance);
    app.build([&]() { return simpleApp(&app); });
    app.createWindow("Custom Widget Demo", 600, 400);
    return app.run();
}