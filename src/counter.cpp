#include "flutter_ui.hpp"
#include <iostream>
#include <sstream>

class CounterApp {
public:
    FlutterUI ui;
    int counter = 0;
    
    CounterApp(HINSTANCE hInstance) : ui(hInstance) {}
    
    void increment() {
        counter++;
        std::cout << "Incremented! Counter = " << counter << std::endl;
        ui.rebuild();
    }
    
    void decrement() {
        counter--;
        std::cout << "Decremented! Counter = " << counter << std::endl;
        ui.rebuild();
    }
    
    void reset() {
        counter = 0;
        std::cout << "Reset! Counter = " << counter << std::endl;
        ui.rebuild();
    }
    
    WidgetPtr buildUI() {
        std::cout << "Building UI with counter = " << counter << std::endl;
        
        // Create dynamic text using stringstream
        std::stringstream ss;
        ss << "Count: " << counter;
        std::string countText = ss.str();
        
        return Center(
            Card(
                Column(
                    Text("Counter Application")
                        ->setFontSize(28)
                        ->setFontWeight(FontWeight::Bold)
                        ->setTextColor(RGB(33, 150, 243)),
                    
                    Divider(),
                    
                    Text(countText)  // Dynamic text - updates on rebuild!
                        ->setFontSize(64)
                        ->setFontWeight(FontWeight::Bold)
                        ->setTextColor(RGB(76, 175, 80)),
                    
                    Divider(),
                    
                    Row(
                        Button("−", [this]() { decrement(); })
                            ->setBackgroundColor(RGB(244, 67, 54))
                            ->setMinWidth(80),
                        
                        Button("Reset", [this]() { reset(); })
                            ->setBackgroundColor(RGB(158, 158, 158))
                            ->setMinWidth(80),
                        
                        Button("+", [this]() { increment(); })
                            ->setBackgroundColor(RGB(76, 175, 80))
                            ->setMinWidth(80)
                    )->setSpacing(10)
                    
                )->setSpacing(20)
                ->setCrossAlignment(Alignment::Center)
            )
        );
    }
    
    int run() {
        ui.createWindow("Flutter-style Counter", 500, 400);
        ui.build([this]() { return buildUI(); });
        return ui.run();
    }
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
                   LPSTR lpCmdLine, int nCmdShow) {
    

    
    CounterApp app(hInstance);
    return app.run();
}