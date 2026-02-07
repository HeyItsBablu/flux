# FlutterUI - Flutter-style UI Framework for Windows

A declarative UI framework for Windows that brings Flutter's widget composition model to C++ with Win32 API.

## Key Features

✅ **Declarative UI** - Build UIs using composable widgets
✅ **Reactive Rebuilds** - Automatic UI updates when state changes
✅ **Builder Pattern** - Fluent API for styling widgets
✅ **Layout System** - Flex-like Row/Column with alignment
✅ **Event Handling** - Lambda-based click handlers
✅ **Modern C++** - Uses C++17 features (shared_ptr, variadic templates, fold expressions)

## Installation

1. Copy `flutter_ui.hpp` to your project
2. Include it: `#include "flutter_ui.hpp"`
3. Compile with C++17 or later: `/std:c++17`

## Quick Start

```cpp
#include "flutter_ui.hpp"

class MyApp {
public:
    FlutterUI ui;
    int counter = 0;
    
    MyApp(HINSTANCE hInstance) : ui(hInstance) {}
    
    void increment() {
        counter++;
        ui.rebuild();  // Triggers UI update
    }
    
    WidgetPtr buildUI() {
        // IMPORTANT: Create dynamic text using stringstream or std::to_string
        std::string countText = "Count: " + std::to_string(counter);
        
        return Center(
            Card(
                Column(
                    Text("My Counter")->setFontSize(24),
                    Text(countText)->setFontSize(48),
                    Button("Increment", [this]() { increment(); })
                )->setSpacing(20)
            )
        );
    }
    
    int run() {
        ui.createWindow("My App", 400, 300);
        ui.build([this]() { return buildUI(); });
        return ui.run();
    }
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    MyApp app(hInstance);
    return app.run();
}
```

## Important: Dynamic Text

When creating text that changes based on state, **always create the string first**:

✅ **CORRECT:**
```cpp
std::string text = "Count: " + std::to_string(counter);
Text(text)
```

❌ **WRONG (won't update):**
```cpp
Text("Count: %d", counter)  // Formats once, won't update!
```

The variadic `Text()` function evaluates format strings immediately, so the value gets "baked in" and won't change on rebuild.

## Widget Reference

### Layout Widgets

#### Container
```cpp
Container(child)
    ->setBackgroundColor(RGB(255, 255, 255))
    ->setPadding(10)
    ->setBorderRadius(8)
```

#### Row / Column
```cpp
Row(widget1, widget2, widget3)
    ->setSpacing(10)
    ->setCrossAlignment(Alignment::Center)

Column(widget1, widget2, widget3)
    ->setSpacing(10)
    ->setCrossAlignment(Alignment::Start)
```

#### Center
```cpp
Center(child)  // Centers child widget
```

#### Padding
```cpp
Padding(20, child)  // Adds padding around child
```

#### SizedBox
```cpp
SizedBox(200, 100, child)  // Fixed size container
```

#### Card
```cpp
Card(child)  // Material-style card with shadow effect
```

### Content Widgets

#### Text
```cpp
Text("Hello World")
    ->setFontSize(24)
    ->setFontWeight(FontWeight::Bold)
    ->setTextColor(RGB(0, 0, 0))
```

#### Button
```cpp
Button("Click Me", [this]() { handleClick(); })
    ->setBackgroundColor(RGB(76, 175, 80))
    ->setMinWidth(120)
```

#### Divider
```cpp
Divider()  // Horizontal line separator
```

## Styling Properties

All widgets support method chaining for styling:

```cpp
widget
    ->setWidth(200)
    ->setHeight(100)
    ->setMinWidth(100)
    ->setMaxWidth(300)
    ->setPadding(10)
    ->setMargin(5)
    ->setBackgroundColor(RGB(255, 255, 255))
    ->setTextColor(RGB(0, 0, 0))
    ->setBorderColor(RGB(200, 200, 200))
    ->setBorderWidth(2)
    ->setBorderRadius(8)
```

## Alignment Options

```cpp
enum class Alignment {
    Start,    // Left/Top
    Center,   // Center
    End,      // Right/Bottom
    Stretch   // Fill available space
};

// Usage:
Column(...)
    ->setAlignment(Alignment::Center)          // Main axis
    ->setCrossAlignment(Alignment::Center)     // Cross axis
```

## Event Handling

Use lambda functions to capture state:

```cpp
Button("Click", [this]() {
    counter++;
    ui.rebuild();
})
```

**Important:** Always call `ui.rebuild()` after changing state to update the UI.

## Rebuild Mechanism

The rebuild system works by:

1. Storing a builder function: `ui.build([this]() { return buildUI(); })`
2. Calling `ui.rebuild()` when state changes
3. The builder function is called, creating a **new widget tree** with updated values
4. Layout is recomputed and UI is redrawn

This is similar to Flutter's `setState()` mechanism.

## Color Reference

Use Windows RGB macro:
```cpp
RGB(255, 0, 0)      // Red
RGB(0, 255, 0)      // Green
RGB(0, 0, 255)      // Blue
RGB(255, 255, 255)  // White
RGB(0, 0, 0)        // Black
```

Common Material Design colors:
```cpp
RGB(244, 67, 54)    // Red
RGB(76, 175, 80)    // Green
RGB(33, 150, 243)   // Blue
RGB(255, 193, 7)    // Amber
RGB(158, 158, 158)  // Gray
```

## Examples

### Simple Counter
See `main.cpp` for a basic counter example.

### Multi-Counter Dashboard
See `advanced_example.cpp` for a more complex example with multiple counters.

### Todo List
```cpp
class TodoApp {
    std::vector<std::string> todos;
    
    WidgetPtr buildUI() {
        auto column = Column();
        
        for (const auto& todo : todos) {
            column->addChild(
                Card(Text(todo))->setMargin(5)
            );
        }
        
        return Container(column);
    }
};
```

## Compilation

### Visual Studio
```
cl /std:c++17 /EHsc main.cpp /link user32.lib gdi32.lib
```

### MinGW
```
g++ -std=c++17 main.cpp -o app.exe -lgdi32 -luser32
```

### CMake
```cmake
cmake_minimum_required(VERSION 3.10)
project(FlutterUIApp)

set(CMAKE_CXX_STANDARD 17)

add_executable(app main.cpp)
target_link_libraries(app gdi32 user32)
```

## Architecture

### Widget Tree
Widgets are organized in a tree structure using `shared_ptr`:
- Parent widgets contain children via `std::vector<WidgetPtr>`
- Builder pattern returns `shared_ptr` for chaining

### Layout Engine
Two-pass layout system:
1. **Measure**: `computeLayout()` calculates widget sizes
2. **Position**: `positionWidget()` assigns x,y coordinates

### Rendering
`Renderer::renderWidget()` recursively draws widgets using GDI:
- Backgrounds with rounded rectangles
- Text with custom fonts
- Borders and shadows

## Debugging

Enable console output:
```cpp
AllocConsole();
FILE* fp;
freopen_s(&fp, "CONOUT$", "w", stdout);

std::cout << "Debug: counter = " << counter << std::endl;
```

## Limitations

- Single window per application
- No scrolling (yet)
- No text input widgets (yet)
- Basic event handling (click only)
- Windows-only (Win32 API)

## Roadmap

- [ ] Text input widgets
- [ ] Scrollable containers
- [ ] Animations
- [ ] More event types (hover, keyboard)
- [ ] Image support
- [ ] Grid layout
- [ ] Theming system

## License

This is a demonstration project. Use freely for learning and prototyping.

## Troubleshooting

### UI doesn't update after clicking
- Make sure you call `ui.rebuild()` in your event handler
- Check that your builder function is capturing `this` correctly

### Text shows old values
- Don't use `Text("Count: %d", counter)` for dynamic values
- Instead: `std::string text = "Count: " + std::to_string(counter); Text(text)`

### Compilation errors
- Ensure C++17 is enabled: `/std:c++17` or `-std=c++17`
- Link against: `user32.lib` and `gdi32.lib`

## Contributing

This is a learning project demonstrating Flutter-style UI in C++. Feel free to extend and improve!