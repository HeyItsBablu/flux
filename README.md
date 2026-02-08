# FlutterUI C++ Framework Documentation

## Overview

FlutterUI is a lightweight, Flutter-inspired UI framework for Windows desktop applications written in C++. It provides a declarative API for building user interfaces using familiar Flutter-like widgets and patterns, while leveraging native Windows GDI for rendering.

## Table of Contents

1. [Core Concepts](#core-concepts)
2. [Architecture](#architecture)
3. [Widget System](#widget-system)
4. [Layout Engine](#layout-engine)
5. [Reactive State Management](#reactive-state-management)
6. [API Reference](#api-reference)
7. [Usage Examples](#usage-examples)
8. [Performance Optimization](#performance-optimization)

---

## Core Concepts

### Declarative UI
FlutterUI follows a declarative paradigm where you describe *what* the UI should look like rather than *how* to construct it. Widgets are composed hierarchically to build complex interfaces.

### Widget Tree
The UI is represented as a tree of widgets, where each widget can have zero or more children. The framework handles layout calculation and rendering automatically.

### Reactive Updates
Changes to application state automatically trigger UI updates through the `State<T>` system, eliminating manual DOM manipulation.

---

## Architecture

### Component Overview

```
┌─────────────────────────────────────────────┐
│           FlutterUI Application             │
└─────────────────┬───────────────────────────┘
                  │
        ┌─────────┴──────────┐
        │                    │
   ┌────▼─────┐      ┌──────▼───────┐
   │  Widget  │      │    State<T>  │
   │   Tree   │◄─────┤   Management │
   └────┬─────┘      └──────────────┘
        │
   ┌────▼─────────┐
   │    Layout    │
   │    Engine    │
   └────┬─────────┘
        │
   ┌────▼─────────┐
   │   Renderer   │
   │   (GDI)      │
   └──────────────┘
```

### Key Components

1. **Widget**: Base class for all UI elements
2. **FlutterUI**: Main application class managing the window and widget tree
3. **LayoutEngine**: Calculates widget positions and sizes
4. **Renderer**: Draws widgets using Windows GDI
5. **State<T>**: Generic reactive state container
6. **FontCache**: Optimizes font resource management

---

## Widget System

### Widget Types

| Type | Description | Use Case |
|------|-------------|----------|
| `Scaffold` | Top-level container with AppBar support | App structure |
| `AppBar` | Material-style app bar | Page headers |
| `Container` | Generic container with styling | Wrapping content |
| `Text` | Display text content | Labels, paragraphs |
| `Button` | Clickable button | User interactions |
| `Row` | Horizontal layout | Side-by-side elements |
| `Column` | Vertical layout | Stacked elements |
| `Padding` | Add spacing around child | Whitespace |
| `Center` | Center child widget | Alignment |
| `SizedBox` | Fixed-size container | Spacing, constraints |
| `Card` | Material-style card | Grouped content |
| `Divider` | Horizontal line separator | Visual separation |
| `Expanded` | Flexible space in Row/Column | Responsive layouts |

### Widget Properties

#### Common Properties

```cpp
// Layout
int x, y;                    // Position (calculated)
int width, height;           // Dimensions
int minWidth, maxWidth;      // Size constraints
int minHeight, maxHeight;
bool autoWidth, autoHeight;  // Auto-sizing flags

// Spacing
int padding;                 // Uniform padding
int paddingLeft, paddingRight, paddingTop, paddingBottom;
int margin;                  // Uniform margin
int marginLeft, marginRight, marginTop, marginBottom;

// Styling
COLORREF backgroundColor;
COLORREF textColor;
COLORREF borderColor;
int borderWidth;
int borderRadius;            // Rounded corners

// Text
int fontSize;
FontWeight fontWeight;       // Light, Normal, Bold
std::string text;
```

#### Alignment Properties

```cpp
Alignment alignment;              // Start, Center, End, Stretch
Alignment crossAlignment;         // Cross-axis alignment
MainAxisAlignment mainAxisAlignment; // Main-axis alignment
int spacing;                      // Spacing between children
```

### Builder Pattern

All widgets support method chaining for easy configuration:

```cpp
auto widget = Text("Hello")
    ->setFontSize(18)
    ->setFontWeight(FontWeight::Bold)
    ->setTextColor(RGB(0, 0, 255))
    ->setPadding(10);
```

---

## Layout Engine

### Layout Algorithm

The layout engine uses a two-pass algorithm:

1. **Measure Pass**: Calculate widget sizes bottom-up
2. **Position Pass**: Position widgets top-down

### Layout Constraints

Widgets receive available space from their parent and determine their actual size within constraints:

```
finalSize = clamp(desiredSize, minSize, maxSize)
```

### Flex Layout (Row/Column)

For Row and Column widgets with `Expanded` children:

1. Calculate fixed-size children first
2. Distribute remaining space proportionally based on `flex` values
3. Apply alignment rules

#### Flex Calculation

```
flexSpace = totalAvailableSpace - fixedChildrenSpace
childSize = (flexSpace × child.flex) / totalFlex
```

### Main Axis Alignment

- **Start**: Children at the beginning
- **End**: Children at the end
- **Center**: Children centered
- **SpaceBetween**: Even spacing between children
- **SpaceAround**: Even spacing around children
- **SpaceEvenly**: Even spacing including edges

---

## Reactive State Management

### State\<T\> Class

The `State<T>` template provides reactive data binding:

```cpp
template <typename T>
class State {
    T value;
    FlutterUI* ui;
    std::vector<std::weak_ptr<Widget>> observers;
    
public:
    State(T initial, FlutterUI* app = nullptr);
    T get() const;
    void set(T newValue);
    void addObserver(std::shared_ptr<Widget> widget);
};
```

### Supported Types

- Arithmetic types (int, float, double, etc.)
- std::string
- Any type with equality operator

### Automatic Updates

When state changes:
1. New value is set
2. All observer widgets are updated
3. Smart invalidation triggers minimal repaints
4. Layout recalculation only if size changes

### Update Optimization

```cpp
// Same-size update → Only repaint
state.set("A");  // Original
state.set("B");  // Same length → just repaint

// Size change → Full layout
state.set("A");      // Original
state.set("Hello");  // Different size → relayout
```

---

## API Reference

### FlutterUI Class

#### Constructor
```cpp
FlutterUI(HINSTANCE hInst);
```

#### Methods

##### build()
```cpp
void build(std::function<WidgetPtr()> buildFunc);
```
Sets the builder function that constructs the widget tree.

##### rebuild()
```cpp
void rebuild();
```
Reconstructs the entire widget tree and triggers layout/paint.

##### createWindow()
```cpp
HWND createWindow(const std::string& title, int width, int height);
```
Creates the main application window.

##### run()
```cpp
int run();
```
Starts the message loop. Blocks until the window is closed.

##### findById()
```cpp
WidgetPtr findById(const std::string& id);
```
Searches the widget tree for a widget with the specified ID.

##### updateWidget()
```cpp
void updateWidget(Widget* widget);
```
Smart update for a specific widget. Chooses between repaint-only or full layout.

##### partialRebuild()
```cpp
void partialRebuild(Widget* widget);
```
Recalculates layout from the widget up to the root.

##### invalidateWidget()
```cpp
void invalidateWidget(Widget* widget);
```
Marks a widget's rectangle for repainting without layout recalculation.

### Widget Factory Functions

#### Container
```cpp
WidgetPtr Container(WidgetPtr child = nullptr);
```
Creates a generic container widget.

#### Text
```cpp
WidgetPtr Text(const std::string& text);
WidgetPtr Text(State<T>& state);  // Reactive variant
```
Creates a text display widget. The state variant automatically updates when state changes.

#### Button
```cpp
WidgetPtr Button(const std::string& text, ClickHandler onClick = nullptr);
```
Creates a material-style button with default green styling.

#### Row
```cpp
WidgetPtr Row(Widgets... widgets);
```
Creates a horizontal layout container. Variadic template accepts multiple children.

#### Column
```cpp
WidgetPtr Column(Widgets... widgets);
```
Creates a vertical layout container. Variadic template accepts multiple children.

#### Padding
```cpp
WidgetPtr Padding(int padding, WidgetPtr child);
```
Wraps a child with uniform padding on all sides.

#### Center
```cpp
WidgetPtr Center(WidgetPtr child);
```
Centers its child both horizontally and vertically.

#### SizedBox
```cpp
WidgetPtr SizedBox(int width, int height, WidgetPtr child = nullptr);
```
Container with fixed dimensions. Can be used for spacing or constraints.

#### Card
```cpp
WidgetPtr Card(WidgetPtr child);
```
Material-style card with shadow effect (simulated with border), rounded corners, and padding.

#### Divider
```cpp
WidgetPtr Divider();
```
Creates a 1-pixel horizontal line separator.

#### Expanded
```cpp
WidgetPtr Expanded(WidgetPtr child, int flex = 1);
```
Makes a child take flexible space in Row or Column. Higher flex values get more space.

#### Scaffold
```cpp
WidgetPtr Scaffold(WidgetPtr appBar = nullptr, WidgetPtr body = nullptr);
```
Creates a material-style app structure with optional AppBar and body content.

#### AppBar
```cpp
WidgetPtr AppBar(const std::string& title);
```
Creates a material-style app bar with title.

### Enumerations

#### Alignment
```cpp
enum class Alignment {
    Start,    // Left/Top
    Center,   // Center
    End,      // Right/Bottom
    Stretch   // Fill available space
};
```

#### MainAxisAlignment
```cpp
enum class MainAxisAlignment {
    Start,        // Pack at start
    Center,       // Center items
    End,          // Pack at end
    SpaceBetween, // Even spacing between items
    SpaceAround,  // Even spacing around items
    SpaceEvenly   // Even spacing including edges
};
```

#### FontWeight
```cpp
enum class FontWeight {
    Light = FW_LIGHT,
    Normal = FW_NORMAL,
    Bold = FW_BOLD
};
```

---

## Usage Examples

### Basic Application

```cpp
#include "FlutterUI.hpp"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    FlutterUI app(hInstance);
    
    app.build([]() {
        return Scaffold(
            AppBar("My Application"),
            Center(
                Text("Hello, FlutterUI!")
                    ->setFontSize(24)
            )
        );
    });
    
    app.createWindow("FlutterUI App", 800, 600);
    return app.run();
}
```

### Counter Example with State

```cpp
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    FlutterUI app(hInstance);
    State<int> counter(0, &app);
    
    app.build([&]() {
        return Scaffold(
            AppBar("Counter App"),
            Center(
                Column(
                    Text("You clicked the button:")
                        ->setFontSize(16),
                    Text(counter)  // Reactive text
                        ->setFontSize(48)
                        ->setFontWeight(FontWeight::Bold)
                        ->setTextColor(RGB(33, 150, 243)),
                    Button("Increment", [&]() {
                        counter.set(counter.get() + 1);
                    })
                )->setSpacing(20)
            )
        );
    });
    
    app.createWindow("Counter", 400, 300);
    return app.run();
}
```

### Complex Layout

```cpp
app.build([&]() {
    return Scaffold(
        AppBar("Dashboard"),
        Padding(20,
            Column(
                Card(
                    Column(
                        Text("Welcome!")
                            ->setFontSize(20)
                            ->setFontWeight(FontWeight::Bold),
                        Divider(),
                        Text("This is a sample dashboard")
                    )->setSpacing(10)
                ),
                
                SizedBox(0, 20),  // Vertical spacing
                
                Row(
                    Expanded(
                        Card(
                            Text("Card 1")
                                ->setFontSize(16)
                        )
                    ),
                    SizedBox(10, 0),  // Horizontal spacing
                    Expanded(
                        Card(
                            Text("Card 2")
                                ->setFontSize(16)
                        ),
                        2  // Takes 2x space
                    )
                ),
                
                SizedBox(0, 20),
                
                Button("Action Button", [&]() {
                    // Handle click
                })
                    ->setBackgroundColor(RGB(76, 175, 80))
            )->setSpacing(0)
        )
    );
});
```

### Dynamic Lists with State

```cpp
State<std::string> statusText("Ready", &app);
int clickCount = 0;

app.build([&]() {
    return Scaffold(
        AppBar("Dynamic UI"),
        Padding(20,
            Column(
                Text(statusText),  // Reactive
                
                Row(
                    Button("Click Me", [&]() {
                        clickCount++;
                        statusText.set("Clicked " + 
                            std::to_string(clickCount) + " times");
                    }),
                    
                    SizedBox(10, 0),
                    
                    Button("Reset", [&]() {
                        clickCount = 0;
                        statusText.set("Reset");
                    })
                )->setMainAxisAlignment(MainAxisAlignment::SpaceEvenly)
            )->setSpacing(20)
        )
    );
});
```

### Custom Styling

```cpp
auto myStyledButton = Button("Custom Button")
    ->setBackgroundColor(RGB(255, 87, 34))  // Deep orange
    ->setTextColor(RGB(255, 255, 255))
    ->setBorderRadius(20)
    ->setPaddingAll(30, 15, 30, 15)
    ->setFontSize(18)
    ->setFontWeight(FontWeight::Bold);

auto myCard = Container(
    Column(
        Text("Custom Card"),
        Divider(),
        Text("With custom styling")
    )->setSpacing(10)
)
->setBackgroundColor(RGB(240, 240, 240))
->setBorderColor(RGB(100, 100, 100))
->setBorderWidth(2)
->setBorderRadius(12)
->setPadding(20);
```

---

## Performance Optimization

### Built-in Optimizations

1. **Font Caching**: Fonts are cached and reused across widgets
2. **Double Buffering**: Eliminates flicker during redraws
3. **Smart Invalidation**: Only updates changed regions when possible
4. **Layout Caching**: Widgets track dirty state to avoid redundant calculations
5. **Weak Pointers**: State observers use `weak_ptr` to prevent memory leaks

### Update Strategy

The framework uses three levels of update efficiency:

1. **Full Rebuild**: `rebuild()` - Reconstructs entire tree
2. **Partial Rebuild**: `partialRebuild()` - Updates widget and parents
3. **Paint Only**: `invalidateWidget()` - Repaints without layout

### State Update Optimization

```cpp
// Automatic optimization
state.set(newValue);  // Framework chooses:
                      // - Paint-only if size unchanged
                      // - Partial layout if size changed
```

### Best Practices

1. **Use State for Dynamic Content**: Avoids manual rebuild calls
2. **Set Fixed Sizes When Possible**: Reduces layout calculations
3. **Minimize Nested Layouts**: Flatten widget tree where reasonable
4. **Use IDs Sparingly**: Only when you need to find widgets later
5. **Batch State Updates**: Set multiple state values before triggering updates

### Memory Management

- Widgets use `std::shared_ptr` for automatic memory management
- State observers use `weak_ptr` to prevent circular references
- Font cache cleaned up automatically on app destruction
- GDI resources (brushes, pens, fonts) properly released

---

## Advanced Topics

### Event Handling

Currently supports click events:

```cpp
widget->setOnClick([&]() {
    // Handle click
});
```

The framework performs automatic hit testing to find the clicked widget in the tree.

### Widget Lifecycle

1. **Construction**: Widget created via factory function
2. **Build**: Added to widget tree
3. **Layout**: Size and position calculated
4. **Paint**: Rendered to screen
5. **Update**: Properties changed, marks dirty
6. **Destruction**: Automatic via shared_ptr

### Custom Widget Creation

While not directly supported, you can compose existing widgets to create reusable components:

```cpp
auto createCustomCard(const std::string& title, const std::string& content) {
    return Card(
        Column(
            Text(title)
                ->setFontSize(18)
                ->setFontWeight(FontWeight::Bold),
            Divider(),
            Text(content)
        )->setSpacing(8)
    );
}
```

### Finding Widgets at Runtime

```cpp
auto widget = app.findById("myWidget");
if (widget) {
    widget->setText("Updated");
    app.updateWidget(widget.get());
}
```

---

## Limitations

1. **Windows Only**: Uses Windows GDI, not cross-platform
2. **Limited Widgets**: Subset of Flutter's widget catalog
3. **No Animations**: Static rendering only
4. **Single Window**: No multi-window support
5. **No Touch**: Mouse input only
6. **Fixed Fonts**: Uses Segoe UI exclusively

---

## Future Enhancements

Potential improvements:
- Animation support
- More widget types (ListView, GridView, etc.)
- Custom font loading
- Image support
- Scrolling containers
- Keyboard input handling
- Accessibility features
- Theme system

---

## License & Credits

This is a custom implementation inspired by Flutter's declarative UI paradigm, adapted for Windows desktop applications using C++ and GDI.

---

## Troubleshooting

### Common Issues

**Widgets not appearing:**
- Ensure `rebuild()` is called after `build()`
- Check that window size is sufficient
- Verify parent constraints allow child to render

**State updates not visible:**
- Pass `&app` to State constructor
- Ensure widget is added to tree before binding state
- Check that state value actually changed

**Layout issues:**
- Verify Expanded widgets are inside Row/Column
- Check that auto-sizing conflicts with fixed sizes
- Ensure constraints are reasonable (min ≤ size ≤ max)

**Memory leaks:**
- Avoid storing raw widget pointers
- Use `shared_ptr` and `weak_ptr` correctly
- Clean up event handlers that capture state by reference

---

## Conclusion

FlutterUI provides a modern, declarative approach to Windows desktop UI development in C++. By borrowing concepts from Flutter while leveraging native Windows APIs, it offers a productive and intuitive way to build desktop applications with reactive state management and automatic layout.