# Flux Widget API Reference

> A declarative, Flutter-inspired widget toolkit for building native Windows UIs.  
> Chain methods, compose layouts, and bind reactive state.

**Platform:** C++ / WIN32

---

## Table of Contents

- [Components](#components)
  - [Component](#component)
  - [CHILD macro](#child-macro)
  - [BuildComponent](#buildcomponent)
  - [Passing state to children](#passing-state-to-children)
  - [deref helper](#deref-helper)
- [Display](#display)
  - [Text](#text)
  - [Divider](#divider)
  - [ProgressBar](#progressbar)
  - [Graph](#graph)
  - [Image](#image)
- [Interaction](#interaction)
  - [Button](#button)
  - [GestureDetector](#gesturedetector)
- [Input](#input)
  - [TextInput](#textinput)
  - [Slider](#slider)
  - [Toggle](#toggle)
  - [CheckBox](#checkbox)
  - [RadioGroup / RadioButton](#radiogroup--radiobutton)
- [Collection](#collection)
  - [ListView](#listview)
  - [GridView](#gridview)
  - [Grid](#grid)
- [State](#state)
  - [Conditional](#conditional)
  - [Switch](#switch)
- [Layout](#layout)
  - [Row](#row)
  - [Column](#column)
  - [Container](#container)
  - [Center](#center)
  - [Expanded](#expanded)
  - [SizedBox](#sizedbox)
  - [Padding](#padding)
- [Structure](#structure)
  - [Scaffold](#scaffold)
  - [AppBar](#appbar)
  - [Card](#card)
- [Overlay](#overlay)
  - [Dropdown](#dropdown)
  - [Tooltip](#tooltip)
  - [Dialog](#dialog)
  - [ContextMenu](#contextmenu)

---

## Components

### Component

Base class for encapsulating stateful UI logic. Use when a section of UI needs its own private state that must survive parent rebuilds.

```cpp
class MyComponent : public Component {
  State<int> count;

public:
  MyComponent() : count(0, context) {}

  WidgetPtr build() override {
    return Column(
        Text(count)->setFontSize(32),
        Button("Increment", [this]() { count.set(count.get() + 1); })
    )->setSpacing(10);
  }
};
```

**Lifecycle**

| Method | Description |
|---|---|
| `build()` | Returns the widget tree. Called once at startup — never again on state change |
| `initState()` | Optional setup hook. Called once after construction |
| `dispose()` | Optional cleanup hook. Called on destruction |

> **Key difference from Flutter:** `build()` is called **once**. State changes flow directly to widgets via the observer system — no rebuild is ever triggered.

---

### CHILD macro

Instantiates a child component inline inside a parent's `build()`. The component is created once and survives for the lifetime of the app — its private state is never reset.

```cpp
CHILD(ComponentType, args...)
```

```cpp
// No args
CHILD(MyComponent)

// With parent state pointer
CHILD(ChildCounter, &count)

// With multiple args
CHILD(ChildForm, &name, &age, &email)
```

> **Safe to use inside `build()`** because `build()` itself is only called once at startup. Unlike Flutter, there is no risk of recreating children on every rebuild.

---

### BuildComponent

Alternative to `CHILD` for top-level component instantiation, typically used inside `app.build()`.

```cpp
BuildComponent<ComponentType>(args...)
```

```cpp
// Top-level entry point
app.build([&]() {
    return FluxApp("My App", BuildComponent<MyComponent>(), AppTheme::light());
});

// Also fine inside build() — called once at startup
WidgetPtr build() override {
    return Column(
        BuildComponent<ChildCounter>(&count)
    );
}
```

**CHILD vs BuildComponent**

| | `CHILD` | `BuildComponent` |
|---|---|---|
| Usage | Inside `build()` | Inside `app.build()` or `build()` |
| Syntax | `CHILD(Type, args...)` | `BuildComponent<Type>(args...)` |
| Behavior | Identical — both create component once |  |

---

### Passing state to children

Parent owns the state and passes a raw pointer to the child. The child can freely read and write via `.get()` and `.set()`. Parent widgets update automatically since they observe the same `State<T>` object — no manual sync needed.

```cpp
// Parent — owns state
class ParentCounter : public Component {
  State<int> count;

public:
  ParentCounter() : count(0, context) {}

  WidgetPtr build() override {
    return Column(
        Text(count)->setFontSize(32),                               // reads count
        Button("Increment", [this]() { count.set(count.get() + 1); }),
        Divider(),
        CHILD(ChildCounter, &count)                                 // shares count
    )->setSpacing(10);
  }
};

// Child — receives pointer, also has its own private state
class ChildCounter : public Component {
  State<int> *count;       // pointer — does not own, parent cannot see childCount
  State<int> childCount;   // private — parent never sees this

public:
  explicit ChildCounter(State<int> *count)
      : count(count), childCount(0, context) {}

  WidgetPtr build() override {
    return Column(
        Text(deref(count))->setFontSize(32),                        // shared state
        Button("Decrement", [this]() { count->set(count->get() - 1); }),
        Text(childCount)->setFontSize(32),                          // private state
        Button("Decrement Child", [this]() { childCount.set(childCount.get() - 1); })
    )->setSpacing(10);
  }
};
```

**State ownership rules**

| | Parent | Child |
|---|---|---|
| `State<int> count` | Owns, reads, writes | — |
| `State<int> *count` | Cannot see | Reads and writes via pointer |
| `State<int> childCount` | Cannot see | Owns, reads, writes |

**Multiple state arguments**

```cpp
class ChildForm : public Component {
  State<std::string> *name;
  State<int>         *age;
  State<bool>        *enabled;

public:
  ChildForm(State<std::string> *name, State<int> *age, State<bool> *enabled)
      : name(name), age(age), enabled(enabled) {}

  WidgetPtr build() override {
    return Column(
        TextInput("Name")->setInputValue(deref(name)),
        Slider(0, 100, 1)->setValue(deref(age)),
        Toggle("Enabled")->setValue(deref(enabled))
    )->setSpacing(8);
  }
};

// Parent passes all three
CHILD(ChildForm, &name, &age, &enabled)
```

---

### deref helper

Converts a `State<T>*` pointer to a `State<T>&` reference so it works with all widget APIs that expect a reference — no extra overloads needed anywhere.

```cpp
template <typename T>
State<T>& deref(State<T> *state);
```

```cpp
// Without deref — won't compile, widget APIs expect a reference
Text(count)                                     //  count is a pointer

// With deref — works with every widget that accepts State<T>
Text(deref(count))                              // 
Text(deref(count), [](int v) { return "Value: " + std::to_string(v); }) // 
TextInput("...")->setInputValue(deref(text))    // 
Slider(0, 100, 1)->setValue(deref(value))       // 
Toggle("...")->setValue(deref(enabled))         // 
CheckBox("...")->setInputValue(deref(checked))  // 
Dropdown(opts)->setSelectedValue(deref(selected)) // 
ProgressBar()->setValue(deref(progress))        // 
Conditional(deref(flag))->Then(...)->Else(...)  // 
```

> Defined in `flux_state.hpp`. Available everywhere via `#include "flux.hpp"`.

**When to use deref**

| Context | Syntax |
|---|---|
| Component owns the state | `Text(count)` — direct reference, no deref needed |
| Component receives a pointer | `Text(deref(count))` — deref required |

---

## Display

### Text

Renders a string of text. Auto-sizes to its content by default.

```cpp
// Basic usage
auto label = Text("Hello, world!")
    ->setFontSize(18)
    ->setFontWeight(FontWeight::Bold)
    ->setTextColor(RGB(30, 30, 30))
    ->setPadding(12);

// Bound to state
auto counter = Text(myState);

// With transform
auto label2 = Text(count, [](int v){ return "Count: " + std::to_string(v); });
```

**Factory**

| Signature | Description |
|---|---|
| `Text(string)` | Static text |
| `Text(State<T>)` | Reactive text, auto-updates when state changes |
| `Text(State<T>, transform)` | Reactive text with a custom format function |

**Methods**

| Method | Type | Description |
|---|---|---|
| `setText(string)` | `string` | Set or change the displayed text |
| `setFontSize(size)` | `int` | Font size in points |
| `setFontWeight(weight)` | `FontWeight` | Normal or Bold |
| `setTextColor(color)` | `COLORREF` | Text color |
| `setHoverTextColor(color)` | `COLORREF` | Text color on mouse hover |
| `setPadding(p)` | `int` | Uniform padding on all sides |
| `setBackgroundColor(color)` | `COLORREF` | Fill background behind text |
| `setBorderRadius(r)` | `int` | Corner rounding for background |
| `setWidth(w)` | `int` | Fixed width (disables auto-sizing) |
| `setHeight(h)` | `int` | Fixed height (disables auto-sizing) |
| `setMinWidth(w)` | `int` | Minimum width constraint |

---

### Divider

A 1px horizontal rule. Fills available width automatically.

```cpp
auto sep = Divider();  // 1px light gray horizontal line
```

| Signature | Description |
|---|---|
| `Divider()` | 1px divider, `RGB(224,224,224)`, full width |

---

### ProgressBar

Horizontal progress indicator. Supports solid or multi-stop gradient fills, reactive state binding, and rounded corners.

```cpp
// Static value
ProgressBar(0.65)
    ->setProgressColors({ RGB(33,150,243), RGB(0,200,150) })
    ->setHeight(8)
    ->setBorderRadius(4);

// Reactive
State<double> progress(0.0, &app);
ProgressBar()->setValue(progress);
```

**Methods**

| Method | Type | Description |
|---|---|---|
| `setValue(v)` | `double` 0–1 | Set fill level statically |
| `setValue(State<double>)` | State | Reactive fill level binding |
| `setProgressColors(colors)` | `vector<COLORREF>` | Solid or gradient fill colors |
| `setBackgroundColor(color)` | `COLORREF` | Track background color |
| `setBorderColor(color)` | `COLORREF` | Track border color |
| `setBorderWidth(w)` | `int` | Track border thickness |
| `setBorderRadius(r)` | `int` | Corner rounding |
| `setHeight(h)` | `int` | Bar height (default 12px) |

---

### Graph

OpenGL-rendered chart widget. Supports Line, Bar, and Area graph types with reactive `State<vector<float>>` data binding, axis labels, titles, grid, and legend.

```cpp
// Static line chart
Graph(500, 300)
    ->addSeries("Temperature", {22,24,27,23,19}, 1.0f, 0.4f, 0.2f)
    ->setTitle("Daily Temps")
    ->setXLabels({"Mon","Tue","Wed","Thu","Fri"});

// Reactive binding
State<std::vector<float>> cpuData;
Graph(600, 300)
    ->addSeries("CPU", cpuData, 0.0f, 1.0f, 0.4f)
    ->setType(GraphType::Area);
```

**Factory**

| Signature | Description |
|---|---|
| `Graph()` | Default 400×300 graph |
| `Graph(w, h)` | Fixed-size graph |

**Methods**

| Method | Type | Description |
|---|---|---|
| `addSeries(label, values, r, g, b)` | `string`, `vector<float>` | Add a static data series |
| `addSeries(label, State<...>, r,g,b)` | State binding | Reactive series — repaints on state change |
| `bindSeries(idx, State)` | `int`, State | Retrofit reactive binding to existing series |
| `setType(type)` | `GraphType` | `Line` · `Bar` · `Area` |
| `setTitle(t)` | `string` | Chart title (top center) |
| `setXLabels(labels)` | `vector<string>` | X-axis tick labels |
| `setYRange(min, max)` | `float, float` | Manual Y-axis range (disables autoRange) |
| `setShowGrid(v)` | `bool` | Toggle background grid lines |
| `clearSeries()` | — | Remove all series |
| `setSize(w, h)` | `int, int` | Resize the graph widget |

---

### Image

Renders an image file via GDI+. Supports five fit modes, rounded corners, aspect-ratio-preserving auto-sizing, and graceful error/placeholder states.

```cpp
// Basic
Image("photo.jpg")
    ->setWidth(300)
    ->setHeight(200)
    ->setFit(ImageFit::Cover);

// Circle avatar
Image("avatar.png")
    ->setWidth(64)
    ->setHeight(64)
    ->setBorderRadius(32);

// Lazy-loaded
auto img = Image()->setWidth(200)->setHeight(200);
// ... later:
img->setImagePath("loaded.jpg");
```

**ImageFit Modes**

| Value | Description |
|---|---|
| `ImageFit::Fill` | Stretch to fill exactly — may distort aspect ratio |
| `ImageFit::Contain` | Scale to fit inside bounds — letterbox if needed (default) |
| `ImageFit::Cover` | Scale to cover bounds — crops edges if needed |
| `ImageFit::None` | Display at original pixel size, centered |
| `ImageFit::ScaleDown` | Like None, but scales down if image exceeds container |

**Methods**

| Method | Type | Description |
|---|---|---|
| `setImagePath(path)` | `string` | Load or swap image at runtime |
| `setFit(mode)` | `ImageFit` | Sizing/cropping mode |
| `setWidth(w)` | `int` | Fixed width (disables auto-sizing) |
| `setHeight(h)` | `int` | Fixed height (disables auto-sizing) |
| `setBorderRadius(r)` | `int` | Corner rounding — half of w/h for a circle |
| `setPadding(p)` | `int` | Inner padding around image |
| `setPlaceholderColor(c)` | `COLORREF` | Fill shown before image loads |
| `setErrorColor(c)` | `COLORREF` | Fill shown on load error |

> **Note:** Requires GDI+ initialized at app startup via `GdiplusInitializer`. Auto-sizes to native image dimensions when both `autoWidth` and `autoHeight` are true.

---

## Interaction

### Button

A clickable widget with a background. Accepts either a text label or a child widget for fully custom content. Darkens slightly on press.

```cpp
// Text button
Button("Save", [&]{ save(); })
    ->setBackgroundColor(RGB(76,175,80))
    ->setBorderRadius(6)
    ->setPadding(12);

// Widget child button
Button(Row(Icon(...), Text("Upload")), [&]{ upload(); });
```

**Factory**

| Signature | Description |
|---|---|
| `Button(text, onClick)` | Text label button |
| `Button(child, onClick)` | Widget child button |

**Methods**

| Method | Type | Description |
|---|---|---|
| `setOnClick(handler)` | `ClickHandler` | Click callback |
| `setChild(widget)` | `WidgetPtr` | Replace content widget |
| `setBackgroundColor(color)` | `COLORREF` | Button background |
| `setHoverBackgroundColor(color)` | `COLORREF` | Background on hover |
| `setTextColor(color)` | `COLORREF` | Label text color |
| `setBorderRadius(r)` | `int` | Corner rounding |
| `setPadding(p)` | `int` | Uniform padding |
| `setPaddingAll(l,t,r,b)` | `int ×4` | Per-side padding |
| `setWidth(w)` | `int` | Fixed width |
| `setHeight(h)` | `int` | Fixed height |

---

### GestureDetector

Wraps any widget and attaches pointer/gesture callbacks without any WndProc edits. Hooks directly into the widget tree's virtual mouse methods.

```cpp
GestureDetector(Card(Text("Click me")))
    ->setOnTap([&]{ handleTap(); })
    ->setOnDoubleTap([&]{ handleDouble(); })
    ->setOnLongPress([&]{ showMenu(); })
    ->setOnDragUpdate([&](int dx, int dy){ pan(dx, dy); })
    ->setOnScrollUp([&](int delta){ zoom(delta); });
```

> **Thresholds:** Long press fires after `500ms`. Double-tap window is `300ms`. Drag starts after `5px` of movement. Mouse capture is set automatically during drags.

**Callbacks**

| Method | Signature | Description |
|---|---|---|
| `setOnTap` | `void()` | Single click / tap |
| `setOnDoubleTap` | `void()` | Two taps within 300 ms |
| `setOnLongPress` | `void()` | Press held for 500 ms |
| `setOnSecondaryTap` | `void()` | Right-click |
| `setOnHoverEnter` | `void()` | Cursor enters bounds |
| `setOnHoverExit` | `void()` | Cursor leaves bounds |
| `setOnPointerMove` | `void(x, y)` | Mouse position while inside |
| `setOnDragStart` | `void()` | Drag threshold exceeded |
| `setOnDragUpdate` | `void(dx, dy)` | Delta since last move during drag |
| `setOnDragEnd` | `void()` | Mouse released after drag |
| `setOnScrollUp` | `void(delta)` | Wheel scrolled upward |
| `setOnScrollDown` | `void(delta)` | Wheel scrolled downward |

---

## Input

### TextInput

Single-line text field with cursor, scrolling, placeholder, and two-way `State<string>` binding.

```cpp
State<std::string> name("", &app);

TextInput("Enter your name...")
    ->setInputValue(name)
    ->setWidth(320);
```

| Method | Type | Description |
|---|---|---|
| `setInputValue(State<string>)` | State | Two-way reactive binding |
| `setPlaceholder(text)` | `string` | Hint shown when empty |

> **Keyboard:** `←/→` move cursor, `Home/End` jump to start/end, `Backspace/Delete` delete characters, blinking cursor via `WM_TIMER`.

---

### Slider

Horizontal range input. Draggable thumb with keyboard support and two-way state binding for `double` or `int` states.

```cpp
State<double> volume(50.0, &app);

Slider(0.0, 100.0, 1.0)
    ->setValue(volume)
    ->setTrackFillColor(RGB(99,102,241))
    ->setOnValueChanged([&](double v){ setVolume(v); });
```

**Factory**

| Signature | Description |
|---|---|
| `Slider(min, max, step)` | Range slider with optional step |

**Methods**

| Method | Type | Description |
|---|---|---|
| `setValue(State<double>)` | State | Two-way double binding |
| `setValue(State<int>)` | State | Two-way int binding |
| `setMinValue(v)` | `double` | Range minimum |
| `setMaxValue(v)` | `double` | Range maximum |
| `setStep(v)` | `double` | Snap step size |
| `setTrackColor(c)` | `COLORREF` | Unfilled track color |
| `setTrackFillColor(c)` | `COLORREF` | Filled track color |
| `setThumbColor(c)` | `COLORREF` | Thumb circle color |
| `setOnValueChanged(fn)` | `void(double)` | Change callback |

---

### Toggle

On/off switch with animated thumb and optional text label. Binds to `State<bool>`.

```cpp
State<bool> darkMode(false, &app);

Toggle("Dark mode")
    ->setValue(darkMode)
    ->setTrackOnColor(RGB(99,102,241))
    ->setOnToggleChanged([&](bool v){ applyTheme(v); });
```

| Method | Type | Description |
|---|---|---|
| `setValue(State<bool>)` | State | Two-way binding |
| `setToggled(bool)` | `bool` | Set initial state |
| `setLabel(text)` | `string` | Text shown beside the toggle |
| `setTrackOnColor(c)` | `COLORREF` | Track color when on |
| `setTrackOffColor(c)` | `COLORREF` | Track color when off |
| `setThumbColor(c)` | `COLORREF` | Thumb color |
| `setOnToggleChanged(fn)` | `void(bool)` | Change callback |

---

### CheckBox

Standard checkbox with optional label. Binds to `State<bool>` via `setInputValue`.

```cpp
State<bool> agreed(false, &app);

CheckBox("I agree to the terms")
    ->setInputValue(agreed);
```

| Signature | Description |
|---|---|
| `CheckBox(label)` | Checkbox with optional text label |
| `setInputValue(State<bool>)` | Two-way bool binding |

---

### RadioGroup / RadioButton

Mutually-exclusive radio buttons. Group manages selection; individual buttons belong to one group. Binds to `State<string>`.

```cpp
State<std::string> plan("free", &app);

RadioGroupWithOptions({
    {"free",  "Free tier"},
    {"pro",   "Pro — $9/mo"},
    {"team",  "Team — $29/mo"},
})->bindValue(plan)
  ->setOnSelectionChanged([&](const std::string& v){ changePlan(v); });

// Manual construction
auto group = RadioGroup();
group->addRadioButton(RadioButton("opt_a", "Option A"));
group->addRadioButton(RadioButton("opt_b", "Option B"));
group->setHorizontal();
```

**RadioGroup Methods**

| Method | Description |
|---|---|
| `addRadioButton(RadioButtonPtr)` | Add a button to the group |
| `bindValue(State<string>)` | Two-way selected-value binding |
| `setSelectedValue(string)` | Set selected value imperatively |
| `setOnSelectionChanged(fn)` | Callback with newly selected value |
| `setHorizontal()` / `setVertical()` | Layout direction |
| `getSelectedValue()` | Returns current selection string |

---

## Collection

### ListView

Scrollable list driven by `State<vector<T>>`. Supports vertical or horizontal orientation, separators, custom scrollbar styling, and thumb drag.

```cpp
State<std::vector<Contact>> contacts(data, &app);

ListView(contacts)
    ->itemBuilder([](int i, const Contact& c) -> WidgetPtr {
        return Card(Text(c.name));
    })
    ->separator([]{ return Divider(); })
    ->setSpacing(8)
    ->setScrollbarColor(RGB(100,100,120));
```

| Method | Description |
|---|---|
| `itemBuilder(fn)` | Builder `(int index, const T&) -> WidgetPtr` |
| `separator(fn)` | Widget inserted between items |
| `setSpacing(px)` | Gap between items |
| `setHorizontal(bool)` | Switch to horizontal scroll |
| `setScrollbarSize(px)` | Scrollbar thickness |
| `setScrollbarColor(c)` | Idle thumb color |
| `setScrollbarHoverColor(c)` | Hover thumb color |
| `setScrollbarActiveColor(c)` | Drag thumb color |
| `setScrollbarTrackColor(c)` | Track background color |

---

### GridView

Scrollable grid driven by `State<vector<T>>`. Supports fixed column count or responsive fixed-cell-width mode.

```cpp
GridView(photos)
    ->columns(3)
    ->itemBuilder([](int i, const Photo& p) -> WidgetPtr {
        return Thumbnail(p);
    })
    ->setSpacing(12);

// Responsive mode
GridView(items)->columnWidth(200)->...
```

| Method | Description |
|---|---|
| `itemBuilder(fn)` | Builder `(int index, const T&) -> WidgetPtr` |
| `columns(n)` | Fixed column count mode |
| `columnWidth(px)` | Responsive mode — derive column count from width |
| `setSpacingH(px)` | Horizontal gap |
| `setSpacingV(px)` | Vertical gap |
| `setSpacing(px)` | Set both H and V spacing |
| `setScrollbarWidth(px)` | Scrollbar thickness |

---

### Grid

Static fixed-column grid for laying out a known set of children. Non-scrolling. Supports alignment, spacing, and responsive cell-width mode.

```cpp
// Fixed 3-column grid
Grid(3,
    Card(Text("A")),
    Card(Text("B")),
    Card(Text("C"))
)->setSpacing(16)
 ->setCrossAlignment(Alignment::Stretch);

// Responsive
GridFixedWidth(200, items...);

// From vector
GridFromList(4, widgetVector);
```

**Factory**

| Signature | Description |
|---|---|
| `Grid(columns, widgets...)` | Fixed column count with variadic children |
| `GridFixedWidth(cellWidth, widgets...)` | Responsive, column count derived from width |
| `GridFromList(columns, vector)` | Fixed columns from a runtime vector |
| `GridFixedWidthFromList(cellWidth, vector)` | Responsive from a runtime vector |

**Methods**

| Method | Description |
|---|---|
| `setColumnCount(n)` | Switch to fixed column count mode |
| `setColumnWidth(px)` | Switch to responsive mode |
| `setSpacing(px)` | Uniform H and V gap |
| `setSpacingH(px)` | Horizontal gap only |
| `setSpacingV(px)` | Vertical gap only |
| `setCrossAlignment(a)` | Cell alignment: Start · Center · End · Stretch |
| `setMainAxisAlignment(a)` | Row distribution: Start · Center · End |
| `setPadding(px)` | Uniform padding |
| `setBackgroundColor(c)` | Grid background fill |
| `setFlex(n)` | Flex factor in parent Flex container |

---

## State

### Conditional

Ternary-style conditional rendering. Rebuilds its child whenever the observed state's predicate changes.

```cpp
// Bool state — no predicate needed
Conditional(isLoggedIn)
    ->Then([]{ return Dashboard(); })
    ->Else([]{ return LoginPage(); });

// Custom predicate on any State<T>
Conditional(itemCount, [](int v){ return v > 0; })
    ->Then([]{ return ItemList(); })
    ->Else([]{ return EmptyState(); });
```

| Method | Description |
|---|---|
| `Then(builder)` | Widget to render when condition is **true** |
| `Else(builder)` | Widget to render when condition is **false** |

---

### Switch

C++-style switch-case conditional rendering. Rebuilds its child whenever the bound state value changes. Works with any comparable `T`.

```cpp
State<int> tabIndex(0, &app);

Switch(tabIndex)
    ->Case(0, []{ return HomePage(); })
    ->Case(1, []{ return ProfilePage(); })
    ->Case(2, []{ return SettingsPage(); })
    ->Default([]{ return ErrorPage(); });
```

| Method | Description |
|---|---|
| `Case(value, builder)` | Widget to render when state equals *value* |
| `Default(builder)` | Fallback widget when no case matches |

---

## Layout

### Row

Lays children out horizontally. Supports flex expansion, spacing, cross-axis alignment, and main-axis distribution.

```cpp
Row(
    Text("Label"),
    Expanded(TextInput()),
    Button("Send")
)->setSpacing(8)
 ->setCrossAlignment(Alignment::Center);
```

| Method | Type | Description |
|---|---|---|
| `setSpacing(px)` | `int` | Gap between children |
| `setCrossAlignment(a)` | `Alignment` | Vertical alignment: Start · Center · End · Stretch |
| `setMainAxisAlignment(a)` | `MainAxisAlignment` | Horizontal distribution: Start · Center · End |
| `setWidth(w)` | `int` | Fixed width |
| `setHeight(h)` | `int` | Fixed height |
| `setPadding(p)` | `int` | Uniform padding |
| `setBackgroundColor(c)` | `COLORREF` | Row background fill |
| `setFlex(n)` | `int` | Flex factor when nested in a Row/Column |

---

### Column

Lays children out vertically. Supports flex expansion, spacing, cross-axis alignment, and main-axis distribution.

```cpp
Column(
    AppBar("Title"),
    Expanded(ListView(items)...),
    BottomBar(...)
)->setSpacing(0)
 ->setMainAxisAlignment(MainAxisAlignment::Start);
```

| Method | Type | Description |
|---|---|---|
| `setSpacing(px)` | `int` | Gap between children |
| `setCrossAlignment(a)` | `Alignment` | Horizontal alignment: Start · Center · End · Stretch |
| `setMainAxisAlignment(a)` | `MainAxisAlignment` | Vertical distribution: Start · Center · End |
| `setWidth(w)` | `int` | Fixed width |
| `setHeight(h)` | `int` | Fixed height |
| `setPadding(p)` | `int` | Uniform padding |
| `setBackgroundColor(c)` | `COLORREF` | Column background fill |
| `setFlex(n)` | `int` | Flex factor when nested in a Row/Column |

---

### Container

Single-child box with full styling support — background, border, radius, padding, margin, size constraints, and hover effects. The most versatile layout primitive.

```cpp
Container(Text("Hello"))
    ->setBackgroundColor(RGB(240,248,255))
    ->setBorderColor(RGB(33,150,243))
    ->setBorderWidth(1)
    ->setBorderRadius(8)
    ->setPadding(16)
    ->setHoverBackgroundColor(RGB(220,240,255));

// State-driven background
Container(child)
    ->setBackgroundColor(isSelected, RGB(230,245,255), RGB(255,255,255));
```

| Method | Type | Description |
|---|---|---|
| `setBackgroundColor(color)` | `COLORREF` | Fill color |
| `setBackgroundColor(State, true, false)` | State + `COLORREF ×2` | Reactive background based on bool state |
| `setHoverBackgroundColor(color)` | `COLORREF` | Fill on mouse hover |
| `setBorderColor(color)` | `COLORREF` | Border stroke color |
| `setHoverBorderColor(color)` | `COLORREF` | Border color on hover |
| `setBorderWidth(w)` | `int` | Border stroke thickness |
| `setBorderRadius(r)` | `int` | Corner rounding |
| `setPadding(p)` | `int` | Uniform inner padding |
| `setPaddingAll(l,t,r,b)` | `int ×4` | Per-side inner padding |
| `setMargin(m)` | `int` | Uniform outer margin |
| `setMarginAll(l,t,r,b)` | `int ×4` | Per-side outer margin |
| `setWidth(w)` | `int` | Fixed width |
| `setHeight(h)` | `int` | Fixed height |
| `setMinWidth(w)` | `int` | Minimum width constraint |
| `setMinHeight(h)` | `int` | Minimum height constraint |
| `setMaxWidth(w)` | `int` | Maximum width constraint |
| `setMaxHeight(h)` | `int` | Maximum height constraint |
| `setFlex(n)` | `int` | Flex factor in parent Row/Column |
| `setOnHover(fn)` | `void(bool)` | Hover enter/leave callback |
| `setBackgroundAlpha(a)` | `BYTE` | Background opacity (0–255) |
| `setBorderAlpha(a)` | `BYTE` | Border opacity (0–255) |

---

### Center

Centers its single child both horizontally and vertically within the available space.

```cpp
Center(Text("No items found")->setTextColor(RGB(150,150,150)));
```

| Signature | Description |
|---|---|
| `Center(child)` | Centers child in available space |

---

### Expanded

Causes its child to fill all remaining space along the main axis of a parent Row or Column. Supports proportional flex ratios.

```cpp
Row(
    Text("Label"),
    Expanded(TextInput()),   // takes all remaining width
    Button("Go")
);

// Proportional flex — 2:1 split
Row(
    Expanded(Column(...), 2),  // 2/3 of width
    Expanded(Column(...), 1)   // 1/3 of width
);
```

| Signature | Description |
|---|---|
| `Expanded(child, flex = 1)` | Flex-expand child with optional flex weight |

---

### SizedBox

A fixed-size box. Used as a spacer (no child) or to constrain a child to an exact size.

```cpp
SizedBox(0, 24);               // 24px vertical gap
SizedBox(16, 0);               // 16px horizontal gap
SizedBox(200, 48, Button("Submit"));  // constrained child
```

| Signature | Description |
|---|---|
| `SizedBox(w, h)` | Fixed-size spacer |
| `SizedBox(w, h, child)` | Child constrained to w × h |

---

### Padding

Wraps a child in uniform padding. Convenience shorthand for `Container` with a single padding value.

```cpp
Padding(16, Text("Padded content"));
```

| Signature | Description |
|---|---|
| `Padding(p, child)` | Uniform padding on all sides |

---

## Structure

### Scaffold

The root structure widget. Manages the overlay stack used by Dropdown, Tooltip, Dialog, and ContextMenu. Composes an optional AppBar with an Expanded body in a Column.

```cpp
Scaffold(
    AppBar("My App"),
    Column(
        Text("Content goes here")
    )
);
```

> **Overlay zIndex order:** Tooltip = 50, Dropdown = 100, ContextMenu = 150, Dialog = 200. Higher zIndex renders on top.

| Signature | Description |
|---|---|
| `Scaffold(appBar, body)` | Root scaffold with optional AppBar and body |

**Overlay API (ScaffoldWidget)**

| Method | Description |
|---|---|
| `addOverlay(widget, renderer, zIndex)` | Register a floating overlay renderer |
| `removeOverlay(widget)` | Unregister a floating overlay |
| `clearOverlays()` | Remove all active overlays |
| `hasOverlays()` | Returns true if any overlays are active |
| `getTopmostOverlay()` | Returns the topmost overlay widget pointer |

---

### AppBar

A 56px tall header bar with a blue background and bold white title text.

```cpp
AppBar("Dashboard");
```

| Signature | Description |
|---|---|
| `AppBar(title)` | Header bar with title string. Default height 56px, blue background. |

---

### Card

A white rounded-corner box with a light border and 16px padding. Convenience wrapper over `Container` with sensible card defaults.

```cpp
Card(
    Column(
        Text("Card Title")->setFontWeight(FontWeight::Bold),
        Text("Some description text")
    )->setSpacing(8)
);
```

| Signature | Description |
|---|---|
| `Card(child)` | White bg, 1px border, 8px radius, 16px padding on all sides |

---

## Overlay

### Dropdown

A select input that opens a scrollable overlay list. Supports keyboard navigation, scroll-wheel, placeholder, and two-way state binding by index or string value.

```cpp
State<std::string> country("", &app);

Dropdown({"Nepal", "India", "USA", "UK"})
    ->setPlaceholder("Select a country")
    ->setSelectedValue(country)
    ->setOnSelectionChanged([&](int i, const std::string& v){
        handleSelect(v);
    });
```

**Factory**

| Signature | Description |
|---|---|
| `Dropdown(options)` | Dropdown with initial options vector |
| `Dropdown()` | Empty dropdown, set options later |

**Methods**

| Method | Type | Description |
|---|---|---|
| `setOptions(opts)` | `vector<string>` | Set or replace option list |
| `setPlaceholder(text)` | `string` | Text shown when nothing is selected |
| `setSelectedIndex(State<int>)` | State | Two-way binding by index |
| `setSelectedValue(State<string>)` | State | Two-way binding by string value |
| `setOnSelectionChanged(fn)` | `void(int, string)` | Change callback with index and value |
| `setItemHeight(h)` | `int` | Height of each list row (default 32px) |
| `setMaxVisibleItems(n)` | `int` | Max rows before scroll kicks in (default 6) |

> **Keyboard:** `↑/↓` navigate, `Enter/Space` open/confirm, `Escape` close, `Home/End` jump to first/last.

---

### Tooltip

Wraps any widget and shows a floating text bubble on hover. Chains the anchor's existing `onHover` callback so existing hover effects continue to work.

```cpp
Tooltip(
    Button("Delete", [&]{ deleteItem(); }),
    "Permanently removes the item"
)
->setPosition(TooltipPosition::Above)
->setTooltipBackground(RGB(30,30,30))
->setTooltipMaxWidth(200);
```

**Factory**

| Signature | Description |
|---|---|
| `Tooltip(anchor, text)` | Wraps anchor and attaches tooltip text |

**Methods**

| Method | Type | Description |
|---|---|---|
| `setTooltipText(text)` | `string` | Update tooltip content |
| `setPosition(pos)` | `TooltipPosition` | `Above` · `Below` · `Auto` (default: prefer Above) |
| `setTooltipBackground(color)` | `COLORREF` | Bubble background (default dark gray) |
| `setTooltipTextColor(color)` | `COLORREF` | Bubble text color (default white) |
| `setTooltipFontSize(size)` | `int` | Font size inside bubble |
| `setTooltipMaxWidth(w)` | `int` | Max bubble width before word-wrap (default 240px) |

---

### Dialog

A modal overlay with a semi-transparent backdrop and a centered content panel. Dispatches input events to its content tree. Closes on outside click by default.

```cpp
auto dlg = Dialog(
    Column(
        Text("Confirm delete?")->setFontSize(16),
        Row(
            Button("Cancel", [&]{ dlg->close(); }),
            Button("Delete", [&]{ deleteItem(); dlg->close(); })
        )->setSpacing(8)
    )
)->setSize(360, 160)
 ->setCloseOnClickOutside(true);

// Open it later
dlg->open();
```

| Method | Type | Description |
|---|---|---|
| `open()` | — | Show the dialog |
| `close()` | — | Dismiss the dialog |
| `setContent(widget)` | `WidgetPtr` | Set or replace dialog content |
| `setSize(w, h)` | `int, int` | Dialog panel dimensions (default 400×300) |
| `setCloseOnClickOutside(bool)` | `bool` | Close when backdrop is clicked (default true) |
| `setOverlayAlpha(alpha)` | `int` 0–255 | Backdrop opacity (default 128 = 50%) |
| `setOnClose(fn)` | `void()` | Called when dialog is dismissed |

---

### ContextMenu

Attaches a right-click context menu to any anchor widget. Renders at the cursor position with automatic edge clamping. Supports separators, disabled items, and keyboard navigation.

```cpp
ContextMenu(
    Text("Right click me"),
    {
        {"Cut",   [&]{ cut(); }},
        {"Copy",  [&]{ copy(); }},
        ContextMenuItem::Separator(),
        {"Paste", [&]{ paste(); }, false}  // disabled
    }
);
```

**ContextMenuItem**

| Factory | Description |
|---|---|
| `ContextMenuItem(label, action, enabled)` | Action item with optional enabled state |
| `ContextMenuItem::Action(label, action, enabled)` | Explicit action item factory |
| `ContextMenuItem::Separator()` | Visual divider between items |

**ContextMenuWidget Methods**

| Method | Type | Description |
|---|---|---|
| `setMenuItems(items)` | `vector<ContextMenuItem>` | Replace all menu items |
| `setItemHeight(h)` | `int` | Row height (default 28px) |
| `setMinWidth(w)` | `int` | Minimum menu width (default 160px) |
| `setMenuBackground(color)` | `COLORREF` | Menu panel background |
| `setMenuBorder(color)` | `COLORREF` | Menu panel border color |
| `setItemHoverColor(color)` | `COLORREF` | Row highlight on hover |

> **Keyboard:** `↑/↓` navigate, `Enter/Space` activate, `Escape` close, `Home/End` jump to first/last enabled item.