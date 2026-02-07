#include "flutter_ui.hpp"
#include <iostream>

// Simple counter test
class TestApp {
public:
    FlutterUI ui;
    int counter = 0;
    
    TestApp(HINSTANCE hInstance) : ui(hInstance) {}
    
    void increment() {
        counter++;
        std::cout << "Counter is now: " << counter << std::endl;
        ui.rebuild();
    }
    
    WidgetPtr buildUI() {
        std::cout << "buildUI called with counter = " << counter << std::endl;
        
        return Center(
            Card(
                Column(
                    Text("Simple Counter Test")
                        ->setFontSize(24)
                        ->setFontWeight(FontWeight::Bold),
                    
                    Text("Count: %d", counter)
                        ->setFontSize(48)
                        ->setTextColor(RGB(76, 175, 80)),
                    
                    Button("Click Me!", [this]() { increment(); })
                )->setSpacing(20)
            )
        );
    }
    
    int run() {
        ui.createWindow("Counter Test", 400, 300);
        ui.build([this]() { return buildUI(); });  // Key fix: pass lambda!
        return ui.run();
    }
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    TestApp app(hInstance);
    return app.run();
}