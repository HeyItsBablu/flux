#include "flutter_ui.hpp"

// ============================================================================
// APPLICATION CLASS - CLEAN STATE MANAGEMENT
// ============================================================================

class CounterApp {
public:
    FlutterUI ui;
    
    // State variables - just regular C++ variables!
    int counter = 0;
    int clicks = 0;
    
    // Constructor
    CounterApp(HINSTANCE hInstance) : ui(hInstance) {}
    
    // Event handlers - simple and clean!
    void onIncrementPressed() {
        counter++;
        ui.rebuild();
    }
    
    void onDecrementPressed() {
        counter--;
        ui.rebuild();
    }
    
    void onResetPressed() {
        counter = 0;
        ui.rebuild();
    }
    
    void onButtonClicked() {
        clicks++;
        ui.rebuild();
    }
    
    // UI builder function
    WidgetPtr buildUI() {
        // Get status message based on current state
        std::string statusMsg = counter > 10 ? "Wow! That's a lot!" : 
                               counter < 0 ? "Going negative..." : 
                               "Keep counting!";
        
        return Container(
            Column(
                // Header
                Container(
                    Text("Flutter-Like UI Demo")
                        ->setFontSize(32)
                        ->setFontWeight(FontWeight::Bold)
                        ->setTextColor(RGB(33, 150, 243))
                )->setPadding(20)
                  ->setBackgroundColor(RGB(255, 255, 255))
                  ->setBorderColor(RGB(33, 150, 243))
                  ->setBorderWidth(2),
                
                // Divider
                Divider(),
                
                // Counter Card
                Card(
                    Column(
                        Text("Counter Example")
                            ->setFontSize(24)
                            ->setFontWeight(FontWeight::Bold)
                            ->setTextColor(RGB(66, 66, 66)),
                        
                        Text("Current count: %d", counter)
                            ->setFontSize(48)
                            ->setFontWeight(FontWeight::Bold)
                            ->setTextColor(RGB(76, 175, 80)),
                        
                        Row(
                            Button("Increment", [this]() { onIncrementPressed(); })
                                ->setBackgroundColor(RGB(76, 175, 80)),
                            
                            Button("Decrement", [this]() { onDecrementPressed(); })
                                ->setBackgroundColor(RGB(244, 67, 54)),
                            
                            Button("Reset", [this]() { onResetPressed(); })
                                ->setBackgroundColor(RGB(158, 158, 158))
                        )->setSpacing(10)
                          ->setCrossAlignment(Alignment::Center),
                        
                        Text(statusMsg)
                            ->setFontSize(16)
                            ->setTextColor(RGB(255, 152, 0))
                            ->setFontWeight(FontWeight::Bold)
                    )->setSpacing(15)
                )->setMargin(20),
                
                // Click Tracker Card
                Card(
                    Column(
                        Text("Click Tracker")
                            ->setFontSize(20)
                            ->setFontWeight(FontWeight::Bold)
                            ->setTextColor(RGB(66, 66, 66)),
                        
                        Text("Total clicks: %d", clicks)
                            ->setFontSize(18)
                            ->setTextColor(RGB(33, 150, 243)),
                        
                        Button("Click Me!", [this]() { onButtonClicked(); })
                            ->setBackgroundColor(RGB(156, 39, 176))
                            ->setWidth(200)
                    )->setSpacing(10)
                )->setMargin(20),
                
                // Info Card
                Card(
                    Column(
                        Text("Features Demonstrated:")
                            ->setFontSize(18)
                            ->setFontWeight(FontWeight::Bold)
                            ->setTextColor(RGB(66, 66, 66)),
                        
                        Text("• Simple state management with C++ variables")
                            ->setFontSize(14)
                            ->setTextColor(RGB(100, 100, 100)),
                        
                        Column(
                            Text("• Row and Column layouts")
                                ->setFontSize(14)
                                ->setTextColor(RGB(100, 100, 100)),
                            
                            Text("• Cards with rounded corners")
                                ->setFontSize(14)
                                ->setTextColor(RGB(100, 100, 100)),
                            
                            Text("• Customizable colors and fonts")
                                ->setFontSize(14)
                                ->setTextColor(RGB(100, 100, 100)),
                            
                            Text("• Beautiful method chaining")
                                ->setFontSize(14)
                                ->setTextColor(RGB(100, 100, 100)),
                            
                            Text("• Lambda event handlers")
                                ->setFontSize(14)
                                ->setTextColor(RGB(100, 100, 100))
                        )->setSpacing(5)
                    )->setSpacing(10)
                )->setMargin(20)
                  ->setBackgroundColor(RGB(232, 245, 233)),
                
                // Footer
                Divider(),
                
                Center(
                    Text("Built with FlutterUI C++ - Clean and Modern")
                        ->setFontSize(12)
                        ->setTextColor(RGB(158, 158, 158))
                )->setPadding(10)
            )->setSpacing(0)
              ->setCrossAlignment(Alignment::Stretch)
        )->setBackgroundColor(RGB(250, 250, 250));
    }
    
    // Run the application
    int run() {
        ui.createWindow("FlutterUI C++ Demo", 600, 800);
        ui.build([this]() { return buildUI(); });  // Pass lambda that calls buildUI
        return ui.run();
    }
};

// ============================================================================
// MAIN ENTRY POINT
// ============================================================================

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    CounterApp app(hInstance);
    return app.run();
}