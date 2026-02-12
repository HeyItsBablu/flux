#include "flux.hpp"
#include <windows.h>

// Counter Card as a proper StatefulComponent
class CounterCardComponent : public StatefulComponent {
private:
    std::string title;
    COLORREF color;
    State<int> count;
    
public:
    CounterCardComponent(FluxUI* ctx, const std::string& t, COLORREF c) 
        : StatefulComponent(ctx), title(t), color(c), count(0, ctx) {}
    
    WidgetPtr build() override {
        return Card(
            Column(
                Text(title)
                    ->setFontSize(16)
                    ->setFontWeight(FontWeight::Bold)
                    ->setTextColor(color),
                
                SizedBox(0, 10),
                
                Text(count)
                    ->setFontSize(32)
                    ->setFontWeight(FontWeight::Bold)
                    ->setTextColor(color),
                
                SizedBox(0, 10),
                
                Row(
                    Button("+", [this]() { count++; })
                        ->setBackgroundColor(color)
                        ->setPaddingAll(15, 8, 15, 8),
                    
                    Button("-", [this]() { count--; })
                        ->setBackgroundColor(color)
                        ->setPaddingAll(15, 8, 15, 8)
                )
                ->setSpacing(8)
                ->setMainAxisAlignment(MainAxisAlignment::Center)
            )
            ->setSpacing(0)
        )
        ->setMinWidth(180);
    }
};

// Main app - keep components alive
WidgetPtr multiCounterApp(FluxUI* app) {
    // Keep component instances alive as static shared_ptrs
    static auto redCounter = FLUX_CREATE_COMPONENT(CounterCardComponent, app, "Red Counter", RGB(244, 67, 54));
    static auto blueCounter = FLUX_CREATE_COMPONENT(CounterCardComponent, app, "Blue Counter", RGB(33, 150, 243));
    static auto greenCounter = FLUX_CREATE_COMPONENT(CounterCardComponent, app, "Green Counter", RGB(76, 175, 80));
    
    return Scaffold(

        Center(
            Row(
                redCounter->build(),
                blueCounter->build(),
                greenCounter->build()
            )
            ->setSpacing(15)
        )
    );
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    FluxUI app(hInstance);
    app.build([&]() { return multiCounterApp(&app); });
    app.createWindow("Multiple Counters", 700, 400);
    return app.run();
}