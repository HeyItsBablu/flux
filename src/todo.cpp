#include "flux_widget.hpp"
#include "flux_widget_list.hpp"
#include "flux_core.hpp"
#include "flux_state.hpp"
#include <windows.h>
#include <vector>
#include <string>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    FluxUI app(hInstance);

    // State: total number of tasks completed
    State<int> completedCount(0, &app);
    
    // State: current task being typed
    State<std::string> currentTask("", &app);

    // Build the UI
    app.build([&]() {
        return Scaffold(
            AppBar("Todo List"),
            
            Padding(20,
                Column(
                    // Header with stats
                    Card(
                        Row(
                            Text("Tasks Completed: ")
                                ->setFontSize(16)
                                ->setFontWeight(FontWeight::Bold),
                            
                            Text(completedCount)
                                ->setFontSize(16)
                                ->setFontWeight(FontWeight::Bold)
                                ->setTextColor(RGB(76, 175, 80))
                        )
                    ),
                    
                    SizedBox(0, 20),
                    
                    // Sample tasks (static for this example)
                    Card(
                        Column(
                            Text("Buy groceries")
                                ->setFontSize(14)
                                ->setPadding(8),
                            
                            Divider(),
                            
                            Text("Write code")
                                ->setFontSize(14)
                                ->setPadding(8),
                            
                            Divider(),
                            
                            Text("Exercise")
                                ->setFontSize(14)
                                ->setPadding(8)
                        )
                    ),
                    
                    SizedBox(0, 20),
                    
                    // Action buttons
                    Row(
                        Expanded(
                            Button("Complete Task", [&completedCount]() {
                                completedCount++;
                            })
                            ->setBackgroundColor(RGB(76, 175, 80)),
                            1
                        ),
                        
                        SizedBox(20, 0),
                        
                        Expanded(
                            Button("Reset", [&completedCount]() {
                                completedCount.set(0);
                            })
                            ->setBackgroundColor(RGB(244, 67, 54)),
                            1
                        )
                    )
                )
                ->setSpacing(0)
            )
        );
    });

    app.createWindow("Todo List Example", 500, 400);
    return app.run();
}