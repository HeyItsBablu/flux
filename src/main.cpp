#include "flutter_ui.hpp"
#include <iostream>
#include <sstream>

class MultiCounterApp {
public:
    FlutterUI ui;
    int redCounter = 0;
    int greenCounter = 0;
    int blueCounter = 0;
    
    MultiCounterApp(HINSTANCE hInstance) : ui(hInstance) {}
    
    WidgetPtr createCounter(const std::string& title, int value, COLORREF color, 
                           std::function<void()> onIncrement, std::function<void()> onDecrement) {
        std::stringstream ss;
        ss << value;
        
        return Card(
            Column(
                Text(title)
                    ->setFontSize(18)
                    ->setFontWeight(FontWeight::Bold)
                    ->setTextColor(color),
                
                Text(ss.str())
                    ->setFontSize(48)
                    ->setFontWeight(FontWeight::Bold)
                    ->setTextColor(color),
                
                Row(
                    Button("−", onDecrement)
                        ->setBackgroundColor(RGB(200, 200, 200))
                        ->setMinWidth(60),
                    
                    Button("+", onIncrement)
                        ->setBackgroundColor(color)
                        ->setMinWidth(60)
                )->setSpacing(10)
                
            )->setSpacing(10)
            ->setCrossAlignment(Alignment::Center)
        )->setMinWidth(150);
    }
    
    WidgetPtr buildUI() {
        int total = redCounter + greenCounter + blueCounter;
        std::stringstream totalSS;
        totalSS << "Total: " << total;
        
        return Container(
            Column(
                Container(
                    Text("Multi-Counter Dashboard")
                        ->setFontSize(32)
                        ->setFontWeight(FontWeight::Bold)
                        ->setTextColor(RGB(33, 150, 243))
                )->setPadding(20)
                ->setBackgroundColor(RGB(255, 255, 255))
                ->setId("header"),
                
                Padding(20,
                    Row(
                        createCounter("Red", redCounter, RGB(244, 67, 54),
                            [this]() { redCounter++; ui.rebuild(); },
                            [this]() { redCounter--; ui.rebuild(); }),
                        
                        createCounter("Green", greenCounter, RGB(76, 175, 80),
                            [this]() { greenCounter++; ui.rebuild(); },
                            [this]() { greenCounter--; ui.rebuild(); }),
                        
                        createCounter("Blue", blueCounter, RGB(33, 150, 243),
                            [this]() { blueCounter++; ui.rebuild(); },
                            [this]() { blueCounter--; ui.rebuild(); })
                    )->setSpacing(20)
                    ->setCrossAlignment(Alignment::Start)
                ),
                
                Container(
                    Column(
                        Text(totalSS.str())
                            ->setFontSize(24)
                            ->setFontWeight(FontWeight::Bold),
                        
                        Button("Reset All", [this]() {
                            redCounter = greenCounter = blueCounter = 0;
                            ui.rebuild();
                        })->setBackgroundColor(RGB(158, 158, 158))
                        ->setMinWidth(150)
                        
                    )->setSpacing(10)
                    ->setCrossAlignment(Alignment::Center)
                )->setPadding(20)
                ->setBackgroundColor(RGB(245, 245, 245))
                
            )->setSpacing(0)
        );
    }
    
    int run() {
        ui.createWindow("Multi-Counter Dashboard", 700, 500);
        ui.build([this]() { return buildUI(); });
        return ui.run();
    }
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
                   LPSTR lpCmdLine, int nCmdShow) {
    

    
    MultiCounterApp app(hInstance);
    return app.run();
}