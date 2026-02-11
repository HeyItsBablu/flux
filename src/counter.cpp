#include "flux.hpp"

#include <windows.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // Create the FluxUI application
    FluxUI app(hInstance);

    // Create a counter state
    State<int> counter(0, &app);

    // Build the UI
    app.build([&counter]() {
        return Scaffold(
            
            Center(
                Column(
                    // Display the counter value
                    Text(counter)
                        ->setFontSize(48)
                        ->setFontWeight(FontWeight::Bold)
                        ->setTextColor(RGB(33, 150, 243)),
                    
                    SizedBox(0, 20),  // Spacer
                    
                    // Row of buttons
                    Row(
                        Button("Decrement", [&counter]() {
                            counter--;  // Decrease counter
                        }),
                        
                        SizedBox(20, 0),  // Horizontal spacer
                        
                        Button("Increment", [&counter]() {
                            counter++;  // Increase counter
                        })
                    )
                )
                ->setPadding(20)
            )
        );
    });

    // Create and show the window
    app.createWindow("Counter Example", 400, 300);
    
    // Run the application
    return app.run();
}