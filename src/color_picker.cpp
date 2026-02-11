#include "flux_widget.hpp"
#include "flux_widget_list.hpp"
#include "flux_core.hpp"
#include "flux_state.hpp"
#include <windows.h>
#include <sstream>
#include <iomanip>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    FluxUI app(hInstance);

    // RGB color states
    State<int> red(128, &app);
    State<int> green(128, &app);
    State<int> blue(128, &app);
    
    // Computed hex color string
    State<std::string> hexColor("#808080", &app);

    // Helper lambda to update hex color
    auto updateHexColor = [&]() {
        std::ostringstream oss;
        oss << "#" 
            << std::hex << std::setfill('0') << std::setw(2) << red.get()
            << std::setw(2) << green.get()
            << std::setw(2) << blue.get();
        std::string hex = oss.str();
        
        // Convert to uppercase
        for (char &c : hex) {
            if (c >= 'a' && c <= 'f') c = c - 'a' + 'A';
        }
        
        hexColor.set(hex);
    };

    // Listen to RGB changes
    red.listen([&](int) { updateHexColor(); });
    green.listen([&](int) { updateHexColor(); });
    blue.listen([&](int) { updateHexColor(); });

    // Build the UI
    app.build([&]() {
        
        return Scaffold(
           
            
            Center(
                Card(
                    Column(
                        // Title
                        Text("RGB Color Picker")
                            ->setFontSize(24)
                            ->setFontWeight(FontWeight::Bold)
                            ->setTextColor(RGB(33, 150, 243)),
                        
                        SizedBox(0, 30),
                        
                        // Color Preview Box
                        Container(
                            Center(
                                Column(
                                    Text("Preview")
                                        ->setFontSize(16)
                                        ->setFontWeight(FontWeight::Bold)
                                        ->setTextColor(RGB(255, 255, 255)),
                                    
                                    SizedBox(0, 10),
                                    
                                    Text(hexColor)
                                        ->setFontSize(20)
                                        ->setFontWeight(FontWeight::Bold)
                                        ->setTextColor(RGB(255, 255, 255))
                                )
                            )
                        )
                        ->setHeight(120)
                        ->setWidth(120)
                        ->setBackgroundColor(RGB(red.get(), green.get(), blue.get()))
                        ->setBorderRadius(12)
                        ->setPadding(20),
                        
                        SizedBox(0, 30),
                        
                        // Red Channel
                        Column(
                            Row(
                                Text("Red:")
                                    ->setFontSize(16)
                                    ->setFontWeight(FontWeight::Bold)
                                    ->setTextColor(RGB(244, 67, 54))
                                    ->setWidth(80),
                                
                                Text(red)
                                    ->setFontSize(16)
                                    ->setFontWeight(FontWeight::Bold)
                                    ->setTextColor(RGB(244, 67, 54))
                            ),
                            
                            SizedBox(0, 8),
                            
                            Row(
                                Button("-10", [&red]() {
                                    int val = red.get() - 10;
                                    if (val < 0) val = 0;
                                    red.set(val);
                                })
                                ->setBackgroundColor(RGB(244, 67, 54)),
                                
                                SizedBox(5, 0),
                                
                                Button("-1", [&red]() {
                                    if (red.get() > 0) red--;
                                })
                                ->setBackgroundColor(RGB(244, 67, 54)),
                                
                                SizedBox(5, 0),
                                
                                Button("+1", [&red]() {
                                    if (red.get() < 255) red++;
                                })
                                ->setBackgroundColor(RGB(244, 67, 54)),
                                
                                SizedBox(5, 0),
                                
                                Button("+10", [&red]() {
                                    int val = red.get() + 10;
                                    if (val > 255) val = 255;
                                    red.set(val);
                                })
                                ->setBackgroundColor(RGB(244, 67, 54))
                            )
                        )
                        ->setSpacing(0),
                        
                        SizedBox(0, 20),
                        
                        // Green Channel
                        Column(
                            Row(
                                Text("Green:")
                                    ->setFontSize(16)
                                    ->setFontWeight(FontWeight::Bold)
                                    ->setTextColor(RGB(76, 175, 80))
                                    ->setWidth(80),
                                
                                Text(green)
                                    ->setFontSize(16)
                                    ->setFontWeight(FontWeight::Bold)
                                    ->setTextColor(RGB(76, 175, 80))
                            ),
                            
                            SizedBox(0, 8),
                            
                            Row(
                                Button("-10", [&green]() {
                                    int val = green.get() - 10;
                                    if (val < 0) val = 0;
                                    green.set(val);
                                })
                                ->setBackgroundColor(RGB(76, 175, 80)),
                                
                                SizedBox(5, 0),
                                
                                Button("-1", [&green]() {
                                    if (green.get() > 0) green--;
                                })
                                ->setBackgroundColor(RGB(76, 175, 80)),
                                
                                SizedBox(5, 0),
                                
                                Button("+1", [&green]() {
                                    if (green.get() < 255) green++;
                                })
                                ->setBackgroundColor(RGB(76, 175, 80)),
                                
                                SizedBox(5, 0),
                                
                                Button("+10", [&green]() {
                                    int val = green.get() + 10;
                                    if (val > 255) val = 255;
                                    green.set(val);
                                })
                                ->setBackgroundColor(RGB(76, 175, 80))
                            )
                        )
                        ->setSpacing(0),
                        
                        SizedBox(0, 20),
                        
                        // Blue Channel
                        Column(
                            Row(
                                Text("Blue:")
                                    ->setFontSize(16)
                                    ->setFontWeight(FontWeight::Bold)
                                    ->setTextColor(RGB(33, 150, 243))
                                    ->setWidth(80),
                                
                                Text(blue)
                                    ->setFontSize(16)
                                    ->setFontWeight(FontWeight::Bold)
                                    ->setTextColor(RGB(33, 150, 243))
                            ),
                            
                            SizedBox(0, 8),
                            
                            Row(
                                Button("-10", [&blue]() {
                                    int val = blue.get() - 10;
                                    if (val < 0) val = 0;
                                    blue.set(val);
                                })
                                ->setBackgroundColor(RGB(33, 150, 243)),
                                
                                SizedBox(5, 0),
                                
                                Button("-1", [&blue]() {
                                    if (blue.get() > 0) blue--;
                                })
                                ->setBackgroundColor(RGB(33, 150, 243)),
                                
                                SizedBox(5, 0),
                                
                                Button("+1", [&blue]() {
                                    if (blue.get() < 255) blue++;
                                })
                                ->setBackgroundColor(RGB(33, 150, 243)),
                                
                                SizedBox(5, 0),
                                
                                Button("+10", [&blue]() {
                                    int val = blue.get() + 10;
                                    if (val > 255) val = 255;
                                    blue.set(val);
                                })
                                ->setBackgroundColor(RGB(33, 150, 243))
                            )
                        )
                        ->setSpacing(0),
                        
                        SizedBox(0, 30),
                        
                        Divider(),
                        
                        SizedBox(0, 20),
                        
                        // Preset Colors
                        Row(
                            Button("Black", [&]() {
                                red.set(0); green.set(0); blue.set(0);
                            })
                            ->setBackgroundColor(RGB(0, 0, 0)),
                            
                            SizedBox(5, 0),
                            
                            Button("White", [&]() {
                                red.set(255); green.set(255); blue.set(255);
                            })
                            ->setBackgroundColor(RGB(255, 255, 255))
                            ->setTextColor(RGB(0, 0, 0)),
                            
                            SizedBox(5, 0),
                            
                            Button("Red", [&]() {
                                red.set(255); green.set(0); blue.set(0);
                            })
                            ->setBackgroundColor(RGB(244, 67, 54)),
                            
                            SizedBox(5, 0),
                            
                            Button("Green", [&]() {
                                red.set(0); green.set(255); blue.set(0);
                            })
                            ->setBackgroundColor(RGB(76, 175, 80)),
                            
                            SizedBox(5, 0),
                            
                            Button("Blue", [&]() {
                                red.set(0); green.set(0); blue.set(255);
                            })
                            ->setBackgroundColor(RGB(33, 150, 243))
                        )
                    )
                    ->setSpacing(0)
                )
                ->setWidth(500)
                ->setPadding(30)
            )
        );
    });

    app.createWindow("Color Picker Example", 600, 700);
    return app.run();
}