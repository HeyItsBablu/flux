#include "flux.hpp"

#include <iostream>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{

    AllocConsole();
    FILE *fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);

    std::cout << "=== FluxUI State API & Layout Tests ===" << std::endl;
    std::cout << "Choose test:" << std::endl;
    std::cout << "1. Counter Test (State API: get, set, update, listen)" << std::endl;
    std::cout << "2. Layout Test (Expanded + Center fix + Reactive state)" << std::endl;

    FluxUI app(hInstance);

    // Create a state with many tasks to ensure scrollbar appears
    State<std::vector<std::string>> tasks({}, &app);

    // Populate with initial tasks
    std::vector<std::string> initialTasks;
    for (int i = 1; i <= 30; i++)
    {
        initialTasks.push_back("Task " + std::to_string(i));
    }
    tasks.set(initialTasks);

    app.build([&]() -> WidgetPtr
              { return Scaffold(
                    AppBar("ListView Mouse Capture Demo"),
                    Column(
                        // Instructions
                        Card(
                            Column(
                                Text("Test Instructions:")
                                    ->setFontWeight(FontWeight::Bold)
                                    ->setMargin(5),
                                Text("1. Click and drag the scrollbar thumb")
                                    ->setFontSize(12)
                                    ->setMargin(2),
                                Text("2. While holding the mouse button, move cursor OUTSIDE the window")
                                    ->setFontSize(12)
                                    ->setMargin(2),
                                Text("3. Release the mouse button outside the window")
                                    ->setFontSize(12)
                                    ->setMargin(2),
                                Text("4. The scrollbar should correctly reset (no longer dragging)")
                                    ->setFontSize(12)
                                    ->setMargin(2)
                                    ->setTextColor(RGB(76, 175, 80))))
                            ->setMargin(10),

                        // ListView with scrollbar
                        Expanded(
                            ListView(tasks)
                                ->itemBuilder([&](int index, const std::string &task) -> WidgetPtr
                                              { return Card(
                                                           Row(

                                                               // Task title
                                                               Text(task)
                                                                   ->setFontSize(14)
                                                                   ->setMargin(10)))
                                                    ->setMargin(5)
                                                    ->setHoverBackgroundColor(RGB(245, 245, 250)); })
                                ->spacing(5)
                                ->setScrollbarColor(RGB(180, 180, 180))
                                ->setScrollbarHoverColor(RGB(140, 140, 140))
                                ->setScrollbarActiveColor(RGB(100, 100, 100))
                                ->setPadding(10)
                                ->setBackgroundColor(RGB(255, 255, 255))))); });

    // Add button controls at bottom
    app.createWindow("ListView Mouse Capture Test", 500, 700);
    return app.run();
}