// Your usage code - OPTIMIZED VERSION:

#include "flutter_ui.hpp"

class ReactiveCounter {
private:
    FlutterUI* ui;
    State<int> counter;
    
public:
    ReactiveCounter(FlutterUI* app) : ui(app), counter(0, app) {}
    
    void increment() {
        counter.set(counter.get() + 1); 
    }
    
    void decrement() {
        counter.set(counter.get() - 1);  
    }
    

    WidgetPtr build() {
        return Center(
            Column(
                Text("Reactive Counter")
                    ->setFontSize(24)
                    ->setFontWeight(FontWeight::Bold)
                    ->setTextColor(RGB(33, 150, 243))
                    ->setMargin(20),
                
                // ✅ This widget is created once and bound to state
                Text(counter)
                    ->setFontSize(48)
                    ->setTextColor(RGB(76, 175, 80))
                    ->setMargin(20),
                
                Row(
                    Button("Decrement", [this]() { decrement(); })
                        ->setBackgroundColor(RGB(244, 67, 54))
                        ->setMargin(10),
                    
                    Button("Increment", [this]() { increment(); })
                        ->setBackgroundColor(RGB(76, 175, 80))
                        ->setMargin(10)
                )
            )
        );
    }
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    FlutterUI app(hInstance);
    ReactiveCounter counterApp(&app);
    
    // ✅ Build is called ONCE during initialization
    app.build([&counterApp]() {
        return counterApp.build();
    });
    
    app.createWindow("Reactive Counter - Optimized!", 400, 300);
    
    return app.run();
}
