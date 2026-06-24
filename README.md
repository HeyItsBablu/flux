<!-- ![Windows](https://github.com/HeyItsBablu/flux/actions/workflows/windows.yml/badge.svg)
![Linux](https://github.com/HeyItsBablu/flux/actions/workflows/linux.yml/badge.svg)
![macOS](https://github.com/HeyItsBablu/flux/actions/workflows/macos.yml/badge.svg)
![Android](https://github.com/HeyItsBablu/flux/actions/workflows/android.yml/badge.svg) -->

# FluxUI

A declarative, cross-platform widget toolkit for C++.  
Chain methods, compose layouts, bind reactive state — one codebase, five platforms.

**Platforms:** Windows · Linux · macOS · Android · Web  
**Compiler:** MSVC 2022 / GCC / Clang / AppleClang  
**Standard:** C++20  
**Renderer:** GDI+ · Cairo · Metal · NanoVG · WebGL2

---

## Quick start

### With scripts (recommended)

Clone the repo, drop your app in `lib/main.cpp`, and run the script for your platform:

```bat
scripts\run-windows.bat
```
```bash
scripts/run-linux.sh
scripts/run-macos.sh
```
```bat
scripts\run-android.bat
scripts\run-web.bat
```

See [INSTALL.md](INSTALL.md) for prerequisites and setup per platform.

```cpp
#include "flux/flux.hpp"

class MyApp : public Widget {
    State<int> counter{0};
public:
    WidgetPtr build() override {
        return Flex({
            Text(counter)->setFontSize(18),
            Button("Click", [this]{ counter++; })
        })
        ->setAlignItems(AlignItems::Center)
        ->setJustifyContent(JustifyContent::Center)
        ->setWidthMode(SizeMode::Full)
        ->setHeightMode(SizeMode::Full)
        ->setDirection(FlexDirection::Column)
        ->setGap(8);
    }
};

WidgetPtr createApp(FluxUI* app) {
    return FluxApp(std::make_shared<MyApp>(), {
        .title  = "My App",
        .theme  = AppTheme::light(),
        .width  = 900,
        .height = 700,
    });
}
```

---

## Screenshots

<p align="center">
  <img src="screenshots/layout.png" width="45%"/>
  <img src="screenshots/counter.png" width="45%"/>
</p>
<p align="center"><em>Layout system · Reactive counter</em></p>

<p align="center">
  <img src="screenshots/graph.png" width="45%"/>
  <img src="screenshots/paint.png" width="45%"/>
</p>
<p align="center"><em>Graph widget · Paint canvas</em></p>

<p align="center">
  <img src="screenshots/photo_editor.png" width="45%"/>
  <img src="screenshots/logic_sim.png" width="45%"/>
</p>
<p align="center"><em>Photo editor · Logic simulator</em></p>

<p align="center">
  <img src="screenshots/illustrator.png" width="60%"/>
</p>
<p align="center"><em>Illustrator-style app</em></p>

## Table of Contents

- [Components](#components)
- [Display](#display)
- [Flex](#flex)
- [FlexBuilder](#flexbuilder)
- [Interaction](#interaction)
- [Input](#input)
- [Collection](#collection)
- [Canvas](#canvas)
- [State](#state)
- [Layout](#layout)
- [Structure](#structure)
- [Overlay](#overlay)
- [Navigation](#navigation)
- [Data](#data)
- [Media](#media)
- [Network](#network)
- [CLI](#cli)

---

## Components

### Widget

Base class for all UI components. Override `build()` to return your widget tree.

```cpp
class MyApp : public Widget {
    State<int> counter{0};
public:
    WidgetPtr build() override {
        return Flex({
            Text(counter)->setFontSize(18),
            Button("Click", [this]{ counter++; })
        })
        ->setWidthMode(SizeMode::Full)
        ->setHeightMode(SizeMode::Full)
        ->setAlignItems(AlignItems::Center)
        ->setJustifyContent(JustifyContent::Center)
        ->setDirection(FlexDirection::Column)
        ->setGap(8);
    }
};

WidgetPtr createApp(FluxUI* app) {
    return FluxApp(std::make_shared<MyApp>(), {
        .title  = "My App",
        .theme  = AppTheme::light(),
        .width  = 900,
        .height = 700,
    });
}
```

| Method | Description |
|---|---|
| `build()` | Returns the widget tree. Called once at startup — never again on state change |
| `onMount()` | Optional setup hook, called once after the widget is first laid out |
| `onDetach()` | Optional cleanup hook, called when the widget is removed from the tree |

> **Key difference from Flutter:** `build()` is called **once**. State changes flow directly to bound widgets via the observer system — no rebuild is triggered.

**Layout properties**

| Property | Type | Description |
|---|---|---|
| `widthMode` / `heightMode` | `SizeMode` | `Fixed` — exact size · `Fit` — shrink to content · `Full` — fill parent |
| `flexGrow` | `int` | How much free space this widget takes (0 = don't grow) |
| `flexShrink` | `int` | Whether this widget shrinks when space is tight (default 1) |
| `flexBasis` | `int` | Starting size before flex is applied (-1 = auto) |
| `padding` / `paddingLeft/Right/Top/Bottom` | `int` | Inner spacing |
| `margin` / `marginLeft/Right/Top/Bottom` | `int` | Outer spacing |
| `minWidth` / `minHeight` | `int` | Size floor |
| `maxWidth` / `maxHeight` | `int` | Size ceiling |
| `alignment` | `Alignment` | Self-alignment within parent |
| `visible` | `bool` | Hidden widgets take no space and receive no events |

**Appearance**

| Property | Type | Description |
|---|---|---|
| `backgroundColor` | `Color` | Fill color (requires `hasBackground = true`) |
| `borderColor` / `borderWidth` / `borderRadius` | — | Border styling (requires `hasBorder = true`) |
| `hoverBackgroundColor` / `hoverTextColor` / `hoverBorderColor` | `Color` | Automatically applied on hover |
| `fontSize` / `fontWeight` / `fontFamily` | — | Text styling inherited by children |

**Events**

| Property | Type | Description |
|---|---|---|
| `onClick` | `ClickHandler` | Fires on left click |
| `onHover` | `HoverHandler` | Fires with `true` on enter, `false` on leave |
| `onRightClick` | `function<bool(int,int)>` | Fires on right click with cursor position |

---

### Passing state to children

The parent owns the state. Children receive it as a `State<T>&` reference in
their constructor — no copies, no wrappers, no special syntax.

```cpp
class CounterDisplay : public Widget {
    State<int>& counter;
public:
    CounterDisplay(State<int>& counter) : counter(counter) {}

    WidgetPtr build() override {
        return Flex({
            Text("Current count:"),
            Text(counter),
            Text(counter, [](int v) {
                return v % 2 == 0 ? "Even" : "Odd";
            })
        })->setHeightMode(SizeMode::Fit);
    }
};

class CounterControls : public Widget {
    State<int>& counter;
public:
    CounterControls(State<int>& counter) : counter(counter) {}

    WidgetPtr build() override {
        return Flex({
            Button("Increment", [this]{ counter++;       }),
            Button("Decrement", [this]{ counter--;       }),
            Button("Reset",     [this]{ counter.set(0);  })
        });
    }
};

class MyApp : public Widget {
    State<int> counter{0};  // owned here
public:
    WidgetPtr build() override {
        return Flex({
            Flex({ Text("Nav") })
                ->setBackgroundColor(Color::fromRGB(50, 50, 150))
                ->setPadding(12)
                ->setWidthMode(SizeMode::Full)
                ->setHeight(50)
                ->setAlignItems(AlignItems::Center)
                ->setJustifyContent(JustifyContent::Center),

            Flex({
                std::make_shared<CounterDisplay>(counter),
                std::make_shared<CounterControls>(counter)
            })->setDirection(FlexDirection::Column)
        })
        ->setDirection(FlexDirection::Column)
        ->setGap(8)
        ->setPadding(16)
        ->setAlignItems(AlignItems::Stretch)
        ->setWidthMode(SizeMode::Full)
        ->setHeightMode(SizeMode::Full);
    }
};
```

A few things to note:

- **Ownership stays in the parent.** `MyApp` declares `State<int> counter{0}` as a member. Children hold a `&` reference — they can read and mutate it but never outlive it.
- **Any child can write.** `CounterControls` calls `counter++`, `counter--`, and `counter.set(0)` directly. Both `CounterDisplay` and any other widget bound to `counter` update automatically.
- **`Text` accepts state directly.** `Text(counter)` renders the current value and re-renders whenever it changes. The optional transform overload `Text(counter, fn)` lets you derive a string from the value — here used to show `"Even"` or `"Odd"`.
- **Pass with `std::make_shared`.** Children are heap-allocated widgets, so pass the reference through the constructor: `std::make_shared<CounterDisplay>(counter)`.

---

## Display

### Text

Renders a string of text. Auto-sizes to its content by default.

```cpp
Text("Hello, world!")
    ->setFontSize(18)
    ->setFontWeight(FontWeight::Bold)
    ->setTextColor(RGB(30, 30, 30));

// Reactive
Text(myState);
Text(count, [](int v){ return "Count: " + std::to_string(v); });

// Full TextStyle
StyledText("Hello", TextStyle{}.setFontSize(20).setBold(true));
```

**Factory**

| Signature | Description |
|---|---|
| `Text(string)` | Static text |
| `Text(State<T>)` | Reactive — auto-updates when state changes |
| `Text(State<T>, transform)` | Reactive with a custom format function |
| `StyledText(string, TextStyle)` | Static text with a full `TextStyle` applied upfront |

**Methods**

| Method | Type | Description |
|---|---|---|
| `setText(string)` | `string` | Set or change displayed text |
| `setText(State<T>)` | State | Reactive text binding |
| `setText(State<T>, transform)` | State + fn | Reactive text with transform |
| `setFontSize(size)` | `int` | Font size in points |
| `setFontWeight(weight)` | `FontWeight` | `Normal` or `Bold` |
| `setFontFamily(family)` | `string` | Font family name |
| `setTextScaleFactor(factor)` | `float` | Scales font size (1.0 = normal) |
| `setTextColor(color)` | `Color` | Text color |
| `setTextColor(State<T>, transform)` | State | Reactive text color |
| `setHoverTextColor(color)` | `Color` | Text color on hover |
| `setLetterSpacing(spacing)` | `float` | Extra space between characters |
| `setWordSpacing(spacing)` | `float` | Extra space between words |
| `setHeight(h)` | `float` | Line height multiplier (1.0 = natural; 1.5 = 50% extra leading) |
| `setTextAlign(align)` | `TextAlign` | `Left · Center · Right · Justify · Start · End` |
| `setTextAlignVertical(align)` | `TextAlignVertical` | `Top · Center · Bottom` |
| `setOverflow(overflow)` | `TextOverflow` | `Clip · Ellipsis · Fade · Visible` |
| `setSoftWrap(wrap)` | `bool` | Word-wrap at boundaries (default `true`) |
| `setMaxLines(lines)` | `int` | Max visible lines; 0 = unlimited |
| `setTextDirection(dir)` | `TextDirection` | `LTR` or `RTL` |
| `setDecoration(decoration)` | `TextDecoration` | Underline, strikethrough, overline |
| `setDecorationColor(color)` | `Color` | Decoration line color |
| `setDecorationStyle(style)` | `TextDecorationStyle` | Solid, dashed, dotted, double, wavy |
| `setDecorationThickness(t)` | `int` | Decoration line thickness |
| `setShadow(shadow)` | `TextShadow` | Single text shadow |
| `setShadows(shadows)` | `vector<TextShadow>` | Multiple text shadows |
| `clearShadows()` | — | Remove all shadows |
| `setTextBackground(color)` | `Color` | Background painted behind each text line |
| `clearTextBackground()` | — | Remove per-line background |
| `setTextStyle(style)` | `TextStyle` | Apply a full `TextStyle` at once |
| `setPadding(p)` | `int` | Uniform padding |
| `setPaddingH(p)` | `int` | Horizontal padding (left + right) |
| `setPaddingV(p)` | `int` | Vertical padding (top + bottom) |
| `setPaddingLRTB(l, r, t, b)` | `int ×4` | Per-side padding |
| `setBackgroundColor(color)` | `Color` | Widget background fill |
| `setBorderRadius(r)` | `int` | Corner rounding for background |
| `setWidth(w)` | `int` | Fixed widget width |
| `setWidgetHeight(h)` | `int` | Fixed widget height |
| `setMinWidth(w)` | `int` | Minimum width constraint |

---

### Icon

Renders a glyph from the `FluxIcons` icon set.

```cpp
Icon(FluxIcons::Settings)
Icon(FluxIcons::Menu, 20)
Icon(state, [](bool v) -> FluxIcons::IconGlyph {
    return v ? FluxIcons::Check : FluxIcons::Close;
})
```

**Factory**

| Signature | Description |
|---|---|
| `Icon(glyph)` | Static glyph at default size (16px) |
| `Icon(glyph, size)` | Static glyph at explicit size |
| `Icon(State<T>, transform)` | Reactive glyph — transform maps `T` to `FluxIcons::IconGlyph` |
| `Icon(State<T>, transform, size)` | Reactive glyph at explicit size |

**Methods**

| Method | Description |
|---|---|
| `setSize(size)` | Icon size in points |
| `setColor(color)` | Icon color |
| `setHoverColor(color)` | Icon color on hover |
| `setIconFontFamily(family)` | Override icon font |
| `setGlyph(glyph)` | Set or change glyph (`FluxIcons::IconGlyph`) |
| `setGlyph(State<T>, transform)` | Reactive glyph binding |

---

### Divider

A 1px horizontal rule that fills available width.

```cpp
Divider()
```

---

### ProgressBar

Horizontal progress indicator with solid or gradient fill.

```cpp
ProgressBar(0.65)
    ->setProgressColors({ RGB(33,150,243), RGB(0,200,150) })
    ->setHeight(8)
    ->setBorderRadius(4);

ProgressBar()->setValue(progressState);
```

**Factory**

| Signature | Description |
|---|---|
| `ProgressBar()` | Progress bar starting at 0.0 |
| `ProgressBar(value)` | Progress bar with initial fill level (0.0–1.0) |

**Methods**

| Method | Type | Description |
|---|---|---|
| `setValue(v)` | `double` 0–1 | Static fill level |
| `setValue(State<double>)` | State | Reactive fill level |
| `setProgressColors(colors)` | `vector<Color>` | Solid or gradient fill |
| `setBackgroundColor(color)` | `Color` | Track background |
| `setBorderColor(color)` | `Color` | Track border |
| `setBorderWidth(w)` | `int` | Border thickness |
| `setBorderRadius(r)` | `int` | Corner rounding |
| `setHeight(h)` | `int` | Bar height (default 12px) |
| `setWidth(w)` | `int` | Fixed width |

---

### Graph

OpenGL-rendered chart widget supporting Line, Bar, and Area types.

```cpp
Graph(500, 300)
    ->addSeries("Temperature", {22,24,27,23,19}, 1.0f, 0.4f, 0.2f)
    ->setTitle("Daily Temps")
    ->setXLabels({"Mon","Tue","Wed","Thu","Fri"});

// Reactive
Graph(600, 300)
    ->addSeries("CPU", cpuDataState, 0.0f, 1.0f, 0.4f)
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
| `addSeries(label, values, r, g, b)` | `string`, `vector<float>` | Add static data series |
| `addSeries(label, State<...>, r,g,b)` | State | Reactive series |
| `bindSeries(idx, State)` | `int`, State | Retrofit reactive binding |
| `setType(type)` | `GraphType` | `Line` · `Bar` · `Area` |
| `setTitle(t)` | `string` | Chart title |
| `setXLabels(labels)` | `vector<string>` | X-axis tick labels |
| `setYRange(min, max)` | `float, float` | Manual Y-axis range |
| `setShowGrid(v)` | `bool` | Toggle grid lines |
| `clearSeries()` | — | Remove all series |
| `setSize(w, h)` | `int, int` | Resize the widget |

---

### Image

Renders an image file with five fit modes. Supports local assets, network URLs, and in-memory buffers. All loading is asynchronous.

```cpp
// Local asset (async)
AssetImage("photo.jpg")
    ->setWidth(300)
    ->setHeight(200)
    ->setFit(ImageFit::Cover);

// Network image
NetworkImage("https://example.com/photo.jpg")
    ->setWidth(300)
    ->setHeight(200);

// In-memory buffer
MemoryImage(myBytes)
    ->setWidth(300);

// Empty widget — load later
Image()
    ->setImagePath("photo.jpg")
    ->setWidth(300);

// Circle avatar
AssetImage("avatar.png")->setWidth(64)->setHeight(64)->setBorderRadius(32);
```

**Factory**

| Signature | Description |
|---|---|
| `Image()` | Empty image widget — call `setImagePath()` or `setUrl()` to load |
| `AssetImage(path)` | Load a local file asynchronously |
| `NetworkImage(url)` | Load from an HTTP/HTTPS URL asynchronously |
| `NetworkImage(url, postToUI)` | As above; `postToUI` controls UI-thread dispatch (default `true`) |
| `MemoryImage(bytes)` | Decode from a `vector<uint8_t>` synchronously |
| `ImageWidget::asset(path)` | Static named constructor — same as `AssetImage` |
| `ImageWidget::network(url, postToUI)` | Static named constructor — same as `NetworkImage` |
| `ImageWidget::memory(bytes)` | Static named constructor — same as `MemoryImage` |

**ImageFit modes**

| Value | Description |
|---|---|
| `ImageFit::Fill` | Stretch to fill — may distort |
| `ImageFit::Contain` | Fit inside bounds, letterbox (default) |
| `ImageFit::Cover` | Fill bounds, crop edges |
| `ImageFit::None` | Original size, positioned by `imageAlignment` |
| `ImageFit::ScaleDown` | Like None but scales down if larger than container |

**Methods**

| Method | Type | Description |
|---|---|---|
| `setImagePath(path)` | `string` | Load or swap a local file at runtime |
| `setUrl(url, postToUI)` | `string, bool` | Load or swap a network URL at runtime |
| `loadFromUrl(url, postToUI)` | `string, bool` | Explicit async network load |
| `setFit(mode)` | `ImageFit` | Sizing/cropping mode |
| `setRepeat(repeat)` | `ImageRepeat` | `NoRepeat · Repeat · RepeatX · RepeatY` |
| `setFilterQuality(quality)` | `FilterQuality` | `None · Low · Medium · High` |
| `setImageAlignment(alignment)` | `Alignment` | Positioning of image within the box (used by `None`/`ScaleDown`) |
| `setTintColor(color)` | `Color` | Color overlay blended over the image |
| `setWidth(w)` | `int` | Fixed width |
| `setHeight(h)` | `int` | Fixed height |
| `setBorderRadius(r)` | `int` | Corner rounding |
| `setPadding(p)` | `int` | Inner padding |
| `setPlaceholderColor(c)` | `Color` | Fill shown while loading |
| `setErrorColor(c)` | `Color` | Fill shown on load error |
| `setLoadingBuilder(fn)` | `() -> WidgetPtr` | Custom widget shown while loading |
| `setErrorBuilder(fn)` | `() -> WidgetPtr` | Custom widget shown on error |

---


## Flex

The core layout widget. Implements a CSS Flexbox-compatible layout engine —
direction, wrapping, alignment, gaps, and scrolling all work the same way.

```cpp
Flex({
    Text("Hello"),
    Button("Click", [this]{ counter++; })
})
->setDirection(FlexDirection::Column)
->setAlignItems(AlignItems::Center)
->setJustifyContent(JustifyContent::Center)
->setGap(8)
->setWidthMode(SizeMode::Full)
->setHeightMode(SizeMode::Full);
```

---

### Direction

| Value | Description |
|---|---|
| `FlexDirection::Row` | Left to right (default) |
| `FlexDirection::RowReverse` | Right to left |
| `FlexDirection::Column` | Top to bottom |
| `FlexDirection::ColumnReverse` | Bottom to top |

---

### Wrap

| Value | Description |
|---|---|
| `FlexWrap::NoWrap` | Single line, items may overflow (default) |
| `FlexWrap::Wrap` | Items wrap to new lines |
| `FlexWrap::WrapReverse` | Items wrap in reverse direction |

---

### JustifyContent

Distribution along the main axis.

| Value | Description |
|---|---|
| `Start` | Pack toward start (default) |
| `End` | Pack toward end |
| `Center` | Center the group |
| `SpaceBetween` | Equal gaps between items, no outer gap |
| `SpaceAround` | Equal gaps around each item |
| `SpaceEvenly` | Equal gaps between items and edges |

---

### AlignItems

Alignment on the cross axis.

| Value | Description |
|---|---|
| `Start` | Align to start edge |
| `End` | Align to end edge |
| `Center` | Center each item |
| `Stretch` | Stretch to fill cross axis (default) |
| `Baseline` | Align to text baseline |

---

### AlignContent

Multi-line distribution on the cross axis. Only meaningful with `FlexWrap::Wrap`.

| Value | Description |
|---|---|
| `Start` | Pack lines toward start |
| `End` | Pack lines toward end |
| `Center` | Center lines as a group |
| `SpaceBetween` | Equal gaps between lines, no outer gap |
| `SpaceAround` | Equal gaps around each line |
| `SpaceEvenly` | Equal gaps between lines and edges |
| `Stretch` | Stretch lines to fill cross axis |

---

### Methods

| Method | Type | Description |
|---|---|---|
| `setDirection(d)` | `FlexDirection` | Main axis direction |
| `setWrap(w)` | `FlexWrap` | Line wrapping behavior |
| `setJustifyContent(j)` | `JustifyContent` | Main axis distribution |
| `setAlignItems(a)` | `AlignItems` | Cross axis alignment per item |
| `setAlignContent(a)` | `AlignContent` | Cross axis distribution of lines |
| `setGap(px)` | `int` | Gap between all items |
| `setScrollable(v)` | `bool` | Enable scroll and fling on the main axis |
| `setPadding(px)` | `int` | Uniform inner padding |
| `setBackgroundColor(c)` | `Color` | Background fill |
| `setBorderColor(c)` | `Color` | Border color |
| `setBorderWidth(w)` | `int` | Border thickness |
| `setBorderRadius(r)` | `int` | Corner rounding |
| `setWidthMode(m)` | `SizeMode` | `Fixed` · `Fit` · `Full` |
| `setHeightMode(m)` | `SizeMode` | `Fixed` · `Fit` · `Full` |
| `setWidth(w)` | `int` | Fixed width (sets mode to Fixed) |
| `setHeight(h)` | `int` | Fixed height (sets mode to Fixed) |
| `setFlexGrow(n)` | `int` | How much free space this item takes (0 = don't grow) |
| `setFlexShrink(n)` | `int` | Whether this item shrinks under pressure (default 1) |
| `setFlexBasis(px)` | `int` | Starting size before flex is applied (-1 = auto) |
| `setOrder(n)` | `int` | Layout order override |
| `responsive(bp, fn)` | `Breakpoint, fn(FlexProps&)` | Override any prop at a breakpoint |

---

### Responsive overrides

Props can be overridden at any breakpoint using a mobile-first cascade.
Overrides stack — `Sm` applies at 640px and up, `Md` at 768px and up, and so on.

```cpp
Flex({ ... })
->setDirection(FlexDirection::Column)       // base (mobile): stacked
->responsive(Breakpoint::Md, [](FlexProps& p) {
    p.direction = FlexDirection::Row;       // tablet+: side by side
    p.gap = 16;
})
->responsive(Breakpoint::Lg, [](FlexProps& p) {
    p.justify = JustifyContent::SpaceBetween;
});
```

Default breakpoint thresholds match Tailwind CSS:

| Breakpoint | Default threshold |
|---|---|
| `Sm` | 640px |
| `Md` | 768px |
| `Lg` | 1024px |
| `Xl` | 1280px |
| `Xxl` | 1536px |

Override globally with:

```cpp
BreakpointProvider::set({ .sm=480, .md=768, .lg=1024 });
```

---

## FlexBuilder

A virtualised, key-aware version of `Flex` for dynamic lists. Items are built
lazily on demand and cached by key. Only visible items are rendered. Layout can
also be skipped for off-screen items when `setVirtualizeLayout(true)` is set
alongside a fixed item extent.

### Static list from a vector

```cpp
std::vector<std::string> items = { "Apple", "Banana", "Cherry" };

FlexBuilder(items,
    [](int i, const std::string& s){ return FlexItemKey::fromIndex(i); },
    [](int i, const std::string& s){ return Text(s); }
)
->setDirection(FlexDirection::Column)
->setScrollable(true)
->setGap(8);
```

### Reactive list from State

```cpp
struct Todo { int64_t id; std::string text; };
State<std::vector<Todo>> todos{{}};

FlexBuilder(todos,
    [](int, const Todo& t){ return FlexItemKey::fromInt64(t.id); },
    [](int, const Todo& t){ return Text(t.text); }
)
->setDirection(FlexDirection::Column)
->setScrollable(true)
->setGap(8)
->setWidthMode(SizeMode::Full);

// Any mutation auto-updates the list
todos.push_back({ nextId++, "Buy milk" });
todos.erase(2);
```

### Virtualised layout for large lists

```cpp
FlexBuilder(items, keyFn, builderFn)
->setDirection(FlexDirection::Column)
->setItemExtent(48)           // every item is exactly 48px tall
->setVirtualizeLayout(true)   // skip layout for off-screen items
->setScrollable(true);
```

---

### Factory overloads

| Signature | Description |
|---|---|
| `FlexBuilder(vector, keyFn, builderFn)` | Static snapshot with stable keys |
| `FlexBuilder(vector, builderFn)` | Static snapshot, index keys — safe for append-only lists |
| `FlexBuilder(State<vector>, keyFn, builderFn)` | Reactive — auto-rebuilds on state change |
| `FlexBuilder(State<vector>, builderFn)` | Reactive, index keys — safe for append-only lists |

---

### Keys

Keys identify each item in the cache across rebuilds. Without stable keys,
deleting or reordering items causes the wrong widget to appear in the wrong slot.

```cpp
FlexItemKey::fromIndex(i)          // position-based — safe only for static lists
FlexItemKey::fromInt64(item.id)    // stable integer id
FlexItemKey::fromString(item.uuid) // stable string id
```

Always provide a `keyFn` when the list can be mutated (insert, delete, reorder).

```cpp
// integer id from a database row
FlexBuilder(todos,
    [](int, const Todo& t){ return FlexItemKey::fromInt64(t.id); },
    [](int, const Todo& t){ return Text(t.text); }
);

// string UUID
FlexBuilder(files,
    [](int, const File& f){ return FlexItemKey::fromString(f.uuid); },
    [](int, const File& f){ return FileRow(f); }
);
```

> Without a `keyFn`, a debug warning fires on first use reminding you to add one.

---

### Methods

**FlexBuilder-specific**

| Method | Type | Description |
|---|---|---|
| `setItemCount(n)` | `int` | Total number of items |
| `setItemBuilder(fn)` | `(int) -> WidgetPtr` | Builder called per item |
| `setKeyFn(fn)` | `(int) -> FlexItemKey` | Stable key per item — required for mutable lists |
| `setItemExtent(px)` | `int` | Fixed item size along the main axis (required for virtualization) |
| `setVirtualizeLayout(v)` | `bool` | Skip layout for off-screen items — requires `setItemExtent` |
| `invalidateItems()` | — | Discard all cached widgets and rebuild |
| `invalidateItem(idx)` | `int` | Discard one cached widget by index |
| `scrollToIndex(idx, animate)` | `int, bool` | Scroll to bring an item into view |
| `scrollToStart()` | — | Scroll to the beginning |
| `scrollToEnd()` | — | Scroll to the end |

**Shared with Flex**

All `Flex` methods are available on `FlexBuilder` as well:
`setDirection`, `setWrap`, `setJustifyContent`, `setAlignItems`, `setAlignContent`,
`setGap`, `setScrollable`, `setPadding`, `setBackgroundColor`, `setBorderColor`,
`setBorderWidth`, `setBorderRadius`, `setWidthMode`, `setHeightMode`,
`setWidth`, `setHeight`, `setFlexGrow`, `setFlexShrink`, `setFlexBasis`,
`setOrder`, `responsive`.

## Interaction

### Button

Clickable widget with a background. Accepts a text label or a child widget.

```cpp
Button("Save", [&]{ save(); })
    ->setBackgroundColor(RGB(76,175,80))
    ->setBorderRadius(6)
    ->setPadding(12);

// Widget child
Button(Row({Icon(FluxIcons::Upload), Text("Upload")}), [&]{ upload(); });
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
| `setBackgroundColor(color)` | `Color` | Button background |
| `setHoverBackgroundColor(color)` | `Color` | Background on hover |
| `setTextColor(color)` | `Color` | Label text color |
| `setBorderRadius(r)` | `int` | Corner rounding |
| `setPadding(p)` | `int` | Uniform padding |
| `setPaddingAll(l, t, r, b)` | `int ×4` | Per-side padding |
| `setWidth(w)` | `int` | Fixed width |
| `setHeight(h)` | `int` | Fixed height |

---

### GestureDetector

Wraps any widget and attaches pointer/gesture callbacks.

```cpp
GestureDetector(Card(Text("Click me")))
    ->setOnTap([&]{ handleTap(); })
    ->setOnDoubleTap([&]{ handleDouble(); })
    ->setOnLongPress([&]{ showMenu(); })
    ->setOnDragUpdate([&](int dx, int dy){ pan(dx, dy); })
    ->setOnScrollUp([&](int delta){ zoom(delta); });

// Shorthand drag
GestureDetector(myWidget, [&](int dx, int dy){ pan(dx, dy); });
```

> Long press fires after 500ms. Double-tap window is 300ms. Drag starts after 5px of movement.

**Factory**

| Signature | Description |
|---|---|
| `GestureDetector(child)` | Wraps child with no initial callbacks |
| `GestureDetector(child, onDrag)` | Shorthand with drag handler |

**Callbacks**

| Method | Signature | Description |
|---|---|---|
| `setOnTap` | `void()` | Single click |
| `setOnDoubleTap` | `void()` | Two taps within 300ms |
| `setOnLongPress` | `void()` | Press held 500ms |
| `setOnSecondaryTap` | `void()` | Right-click |
| `setOnHoverEnter` | `void()` | Cursor enters bounds |
| `setOnHoverExit` | `void()` | Cursor leaves bounds |
| `setOnPointerMove` | `void(x, y)` | Mouse position while inside |
| `setOnDragStart` | `void()` | Drag threshold exceeded |
| `setOnDragUpdate` | `void(dx, dy)` | Delta since last move |
| `setOnDragEnd` | `void()` | Mouse released after drag |
| `setOnScrollUp` | `void(delta)` | Wheel scrolled up |
| `setOnScrollDown` | `void(delta)` | Wheel scrolled down |

---

## Input

### TextInput

Single-line text field with cursor, scroll, placeholder, and two-way `State<string>` binding.

```cpp
TextInput("Enter your name...")
    ->setInputValue(nameState)
    ->setWidth(320);
```

| Method | Type | Description |
|---|---|---|
| `setInputValue(State<string>)` | State | Two-way reactive binding |
| `setPlaceholder(text)` | `string` | Hint shown when empty |
| `setWidth(w)` | `int` | Fixed width |

---

### TextArea

Multiline text input with scrollbars, line numbers, selection, and clipboard support.

```cpp
TextArea("Type your message...")
    ->setInputValue(bodyState)
    ->setWidth(400)
    ->setHeight(200)
    ->setLineNumbers(true);
```

**Methods**

| Method | Type | Description |
|---|---|---|
| `setInputValue(State<string>)` | State | Two-way reactive binding |
| `setPlaceholder(text)` | `string` | Hint shown when empty |
| `setLineNumbers(v)` | `bool` | Show line number gutter |
| `setWordWrap(v)` | `bool` | Enable word wrap |
| `setTabSpaces(n)` | `int` | Spaces per Tab key press |
| `setMaxLength(n)` | `int` | Max character count (0 = unlimited) |
| `setFontSize(s)` | `int` | Font size |
| `setWidth(w)` | `int` | Fixed width |
| `setHeight(h)` | `int` | Fixed height |
| `setFlex(n)` | `int` | Flex factor in parent |
| `setScrollbarSize(s)` | `int` | Scrollbar thickness |
| `setScrollbarColor(c)` | `Color` | Idle thumb color |
| `setScrollbarHoverColor(c)` | `Color` | Hover thumb color |
| `setScrollbarTrackColor(c)` | `Color` | Track background |

> **Keyboard:** `Ctrl+A` select all · `Ctrl+C/X/V` clipboard · `Shift+arrows` extend selection · `PgUp/PgDn` page scroll.

---

### NumberInput / SpinBox

Numeric input with up/down arrow buttons, mouse wheel, and direct keyboard editing.

```cpp
NumberInput(0.0, 100.0, 1.0)
    ->setValue(countState)
    ->setPrefix("$")
    ->setSuffix(" kg")
    ->setDecimalPlaces(2)
    ->setWidth(120);

// Alias
SpinBox(0, 255, 1)->setValue(brightnessState);
```

**Factory:** `NumberInput(min, max, step)` · `SpinBox(min, max, step)`

**Methods**

| Method | Type | Description |
|---|---|---|
| `setValue(State<double>)` | State | Two-way double binding |
| `setValue(State<int>)` | State | Two-way int binding |
| `setMin(v)` | `double` | Minimum value |
| `setMax(v)` | `double` | Maximum value |
| `setStep(v)` | `double` | Increment/decrement step |
| `setDecimalPlaces(n)` | `int` | Decimal digits shown (0 = integer) |
| `setPrefix(s)` | `string` | Text prepended to display value |
| `setSuffix(s)` | `string` | Text appended to display value |
| `setOnValueChanged(fn)` | `void(double)` | Fires on every value change |
| `setWidth(w)` | `int` | Fixed width |
| `setFlex(n)` | `int` | Flex factor in parent |

> **Keyboard:** `↑/↓` step · `PgUp/PgDn` step ×10 · `Home/End` jump to min/max · `Enter` commit typed value · `Escape` revert.

---

### Slider

Horizontal range input with draggable thumb and keyboard support.

```cpp
Slider(0.0, 100.0, 1.0)
    ->setValue(volumeState)
    ->setTrackFillColor(RGB(99,102,241))
    ->setOnValueChanged([&](double v){ setVolume(v); });
```

**Factory:** `Slider(min, max, step)`

**Methods**

| Method | Type | Description |
|---|---|---|
| `setValue(State<double>)` | State | Two-way double binding |
| `setValue(State<int>)` | State | Two-way int binding |
| `setMinValue(v)` | `double` | Range minimum |
| `setMaxValue(v)` | `double` | Range maximum |
| `setStep(v)` | `double` | Snap step size |
| `setTrackColor(c)` | `Color` | Unfilled track color |
| `setTrackFillColor(c)` | `Color` | Filled track color |
| `setThumbColor(c)` | `Color` | Thumb color |
| `setOnValueChanged(fn)` | `void(double)` | Change callback |
| `setWidth(w)` | `int` | Fixed width |

---

### Toggle

On/off switch with animated thumb and optional label. Binds to `State<bool>`.

```cpp
Toggle("Dark mode")
    ->setValue(darkModeState)
    ->setTrackOnColor(RGB(99,102,241))
    ->setOnToggleChanged([&](bool v){ applyTheme(v); });
```

| Method | Type | Description |
|---|---|---|
| `setValue(State<bool>)` | State | Two-way binding |
| `setToggled(bool)` | `bool` | Set initial state |
| `setLabel(text)` | `string` | Text beside the toggle |
| `setTrackOnColor(c)` | `Color` | Track color when on |
| `setTrackOffColor(c)` | `Color` | Track color when off |
| `setThumbColor(c)` | `Color` | Thumb color |
| `setOnToggleChanged(fn)` | `void(bool)` | Change callback |

---

### CheckBox

Standard checkbox with optional label. Binds to `State<bool>`.

```cpp
CheckBox("I agree to the terms")->setInputValue(agreedState);
```

| Signature | Description |
|---|---|
| `CheckBox(label)` | Checkbox with optional text label |
| `setInputValue(State<bool>)` | Two-way bool binding |

---

### RadioGroup / RadioButton

Mutually-exclusive radio buttons bound to `State<string>`.

```cpp
RadioGroupWithOptions({
    {"free",  "Free tier"},
    {"pro",   "Pro — $9/mo"},
    {"team",  "Team — $29/mo"},
})->bindValue(planState)
  ->setOnSelectionChanged([&](const std::string& v){ changePlan(v); });

// Manual
auto group = RadioGroup();
group->addRadioButton(RadioButton("opt_a", "Option A"));
group->addRadioButton(RadioButton("opt_b", "Option B"));
group->setHorizontal();
```

**RadioGroup methods**

| Method | Description |
|---|---|
| `addRadioButton(RadioButtonPtr)` | Add a button to the group |
| `bindValue(State<string>)` | Two-way selected-value binding |
| `setSelectedValue(string)` | Set selected value imperatively |
| `setOnSelectionChanged(fn)` | Callback with newly selected value |
| `setHorizontal()` / `setVertical()` | Layout direction |
| `getSelectedValue()` | Returns current selection |

---

### ColorPicker

HSV color picker with saturation/value square, hue bar, optional alpha bar, and hex display.

```cpp
ColorPicker(RGB(255, 0, 0))
    ->bindValue(brushColorState)
    ->setShowAlpha(false)
    ->setOnColorChanged([&](COLORREF c){ applyColor(c); });
```

**Factory:** `ColorPicker(initialColor)`

**Methods**

| Method | Type | Description |
|---|---|---|
| `setColor(color)` | `COLORREF` | Set color imperatively |
| `getColor()` | `COLORREF` | Read current color |
| `setShowAlpha(show)` | `bool` | Show/hide alpha bar (default `true`) |
| `setOnColorChanged(fn)` | `void(COLORREF)` | Fired on every color change |
| `bindValue(State<COLORREF>)` | State | Two-way reactive binding |

---

### DatePicker

Calendar popup for selecting a date. Includes month/year navigation and a year-range picker.

```cpp
DatePicker()
    ->setDate(FluxDate::today())
    ->setPlaceholder("Select a date")
    ->setOnDateChanged([](FluxDate d) {
        std::cout << d.toString("%d %b %Y") << std::endl;
    });

// Reactive binding
State<FluxDate> selectedDate(FluxDate{}, app);
DatePicker()->setDate(selectedDate);
```

**FluxDate struct**

```cpp
FluxDate d = FluxDate::today();   // today
FluxDate d{2025, 6, 15};         // June 15, 2025
d.toString("%d / %m / %Y");       // "15 / 06 / 2025"
d.isValid();                      // true if year/month/day are set
```

**Methods**

| Method | Type | Description |
|---|---|---|
| `setDate(FluxDate)` | `FluxDate` | Set initial date |
| `setDate(State<FluxDate>)` | State | Two-way reactive binding |
| `setPlaceholder(text)` | `string` | Text when no date selected |
| `setDateFormat(fmt)` | `string` | `strftime`-style format string |
| `setMinDate(date)` | `FluxDate` | Disable dates before this |
| `setMaxDate(date)` | `FluxDate` | Disable dates after this |
| `setOnDateChanged(fn)` | `void(FluxDate)` | Fires when a date is picked |
| `setAccentColor(color)` | `Color` | Header, selection, and indicator color |
| `setWidth(w)` | `int` | Fixed width |

> **Navigation:** Click month/year header to open year picker. `◀ ▶` arrows navigate months or year ranges.

---

### FilePicker

Button-like widget that opens the native OS file dialog on click.

```cpp
// Single file open
FilePicker()
    ->setMode(FilePickerMode::Open)
    ->addFilter("Images", {"*.png","*.jpg","*.jpeg","*.bmp"})
    ->addFilter("All files", {"*.*"})
    ->setDefaultExtension("png")
    ->bindPath(filePath)
    ->setOnChanged([](const std::string& path) {
        std::cout << "Picked: " << path << "\n";
    });

// Save dialog
FilePicker()
    ->setMode(FilePickerMode::Save)
    ->setTitle("Export Image")
    ->setDefaultFilename("output.png")
    ->addFilter("PNG",  {"*.png"})
    ->bindPath(exportPath)
    ->setOnChanged([&](const std::string& p){ surface->exportImage(p); });

// Multiple files
FilePicker()
    ->setMode(FilePickerMode::OpenMultiple)
    ->addFilter("Images", {"*.png","*.jpg"})
    ->bindPaths(paths)
    ->setOnMultiChanged([](const std::vector<std::string>& ps){ ... });

// Folder picker
FilePicker()
    ->setMode(FilePickerMode::Folder)
    ->setTitle("Select output folder")
    ->bindPath(folderPath);
```

**FilePickerMode**

| Value | Description |
|---|---|
| `FilePickerMode::Open` | Single file open dialog |
| `FilePickerMode::OpenMultiple` | Multi-file open dialog |
| `FilePickerMode::Save` | Save / export dialog |
| `FilePickerMode::Folder` | Directory picker |

**Methods**

| Method | Type | Description |
|---|---|---|
| `setMode(m)` | `FilePickerMode` | Dialog type |
| `setTitle(t)` | `string` | Dialog window title |
| `setDefaultFilename(f)` | `string` | Pre-filled filename for Save mode |
| `setDefaultExtension(e)` | `string` | Default file extension |
| `setInitialDir(d)` | `string` | Starting directory |
| `addFilter(label, exts)` | `string, vector<string>` | Add a file type filter |
| `setFilters(fs)` | `vector<FileFilter>` | Replace all filters at once |
| `bindPath(State<string>)` | State | Two-way binding for single path |
| `bindPaths(State<vector<string>>)` | State | Two-way binding for multi-path |
| `setOnChanged(fn)` | `void(string)` | Fires on single-file selection |
| `setOnMultiChanged(fn)` | `void(vector<string>)` | Fires on multi-file selection |
| `setOnCancelled(fn)` | `void()` | Fires when dialog is cancelled |
| `setShowPath(v)` | `bool` | Show selected path beside button |
| `setShowClearBtn(v)` | `bool` | Show × button to clear selection |
| `setPathMaxWidth(w)` | `int` | Max width of the path display |
| `setAccentColor(c)` | `Color` | Accent color for focus ring |
| `setWidth(w)` | `int` | Fixed width |
| `setHeight(h)` | `int` | Fixed height |
| `setFlex(n)` | `int` | Flex factor in parent |
| `open()` | — | Open the dialog programmatically |
| `clear()` | — | Clear the current selection |
| `path()` | `string` | Currently selected single path |
| `paths()` | `vector<string>` | Currently selected paths (multi mode) |
| `hasSelection()` | `bool` | True if a path is selected |

> **Linux async:** On Linux the dialog runs on a background thread via zenity or kdialog. Dispatch results back to the UI by calling `fluxFilePickerDispatchSDLEvent(e)` inside your `SDL_USEREVENT` handler.

---

## Collection

### ListView

Scrollable list. Static initializer-list form or reactive `State<vector<T>>` builder form.

```cpp
// Static
ScrollView({
    Card(Text("Item A")),
    Card(Text("Item B")),
})->setSpacing(8);

// Reactive builder
ListView(contactsState)
    ->itemBuilder([](int i, const Contact& c) -> WidgetPtr {
        return Card(Text(c.name));
    })
    ->separator([]{ return Divider(); })
    ->setSpacing(8);
```

**Factory**

| Signature | Description |
|---|---|
| `ListView({item, item, ...})` | Static list |
| `ListView(State<vector<T>>)` | Reactive list |

**Methods (both modes)**

| Method | Description |
|---|---|
| `setSpacing(px)` | Gap between items |
| `setHorizontal(bool)` | Switch to horizontal scroll |
| `setScrollbarSize(px)` | Scrollbar thickness |
| `setScrollbarColor(c)` | Idle thumb color |
| `setScrollbarHoverColor(c)` | Hover thumb color |
| `setScrollbarActiveColor(c)` | Drag thumb color |
| `setScrollbarTrackColor(c)` | Track background |
| `setPadding(px)` | Inner padding (static mode) |
| `setBackgroundColor(c)` | Background fill (static mode) |
| `setHeight(h)` | Fixed height (static mode) |

**Methods (reactive builder only)**

| Method | Description |
|---|---|
| `itemBuilder(fn)` | Builder `(int index, const T&) -> WidgetPtr` |
| `separator(fn)` | Widget inserted between items |
| `setKeyFn(fn)` | Custom key function for diffing |

---

### GridView

Scrollable grid driven by `State<vector<T>>`.

```cpp
GridView(photosState)
    ->columns(3)
    ->itemBuilder([](int i, const Photo& p) -> WidgetPtr {
        return Thumbnail(p);
    })
    ->setSpacing(12);

// Responsive
GridView(itemsState)->columnWidth(200)->itemBuilder(...);
```

| Method | Description |
|---|---|
| `itemBuilder(fn)` | Builder `(int index, const T&) -> WidgetPtr` |
| `columns(n)` | Fixed column count |
| `columnWidth(px)` | Responsive — derive column count from width |
| `setSpacing(px)` | Set H and V spacing |
| `setSpacingH(px)` | Horizontal gap |
| `setSpacingV(px)` | Vertical gap |
| `setScrollbarWidth(px)` | Scrollbar thickness |
| `setScrollbarColor(c)` | Idle thumb color |
| `setScrollbarHoverColor(c)` | Hover thumb color |
| `setScrollbarActiveColor(c)` | Drag thumb color |
| `setScrollbarTrackColor(c)` | Track background |
| `setKeyFn(fn)` | Custom key function for diffing |

---

### Grid

Static fixed-column grid for a known set of children. Non-scrolling.

```cpp
Grid(3,
    Card(Text("A")),
    Card(Text("B")),
    Card(Text("C"))
)->setSpacing(16);

GridFixedWidth(200, items...);
GridFromList(4, widgetVector);
```

**Factory**

| Signature | Description |
|---|---|
| `Grid(columns, widgets...)` | Fixed columns, variadic children |
| `GridFixedWidth(cellWidth, widgets...)` | Responsive from variadic children |
| `GridFromList(columns, vector)` | Fixed columns from runtime vector |
| `GridFixedWidthFromList(cellWidth, vector)` | Responsive from runtime vector |

**Methods**

| Method | Description |
|---|---|
| `setColumnCount(n)` | Fixed column count |
| `setColumnWidth(px)` | Responsive mode |
| `setSpacing(px)` | Uniform gap |
| `setSpacingH(px)` / `setSpacingV(px)` | Per-axis gap |
| `setCrossAxisAlignment(a)` | `Start · Center · End · Stretch` |
| `setMainAxisAlignment(a)` | `Start · Center · End` |
| `setPadding(px)` | Uniform padding |
| `setPaddingAll(l, t, r, b)` | Per-side padding |
| `setBackgroundColor(c)` | Grid background |
| `setWidth(w)` / `setHeight(h)` | Fixed dimensions |
| `setFlex(n)` | Flex factor in parent |

---

### Accordion

Vertical stack of collapsible panels.

```cpp
#include "flux/flux_accordion.hpp"

AccordionPanel p1("Appearance", "Theme & display");
p1.icon     = L"\uE771";
p1.expanded = true;
p1.body = Column({
    Row({ Text("Dark theme"), Toggle(&darkTheme) })->setSpacing(8),
})->setSpacing(12)->setPadding(8);

auto acc = Accordion({ p1, p2, p3 })
               ->setSingleExpand(true)
               ->setAccentColor(RGB(33, 150, 243))
               ->setOnChanged([](int idx, bool open) { });
```

**AccordionPanel struct**

```cpp
AccordionPanel p("Title", "Optional subtitle");
p.icon     = L"\uE713";
p.expanded = false;
p.disabled = false;
p.body     = myWidget;
```

**Methods**

| Method | Type | Description |
|---|---|---|
| `setSingleExpand(v)` | `bool` | At most one panel open at a time (default `false`) |
| `setOnChanged(fn)` | `void(int, bool)` | Fires on every expand/collapse |
| `expand(idx)` | `int` | Expand panel by index |
| `collapse(idx)` | `int` | Collapse panel by index |
| `toggle(idx)` | `int` | Toggle panel by index |
| `expandAll()` | — | Expand all panels |
| `collapseAll()` | — | Collapse all panels |
| `panelAt(idx)` | `AccordionPanel*` | Mutable access to a panel |
| `panelCount()` | `int` | Number of panels |
| `setPanels(panels)` | `vector<AccordionPanel>` | Replace all panels at runtime |
| `setHeaderHeight(h)` | `int` | Header row height (default 48px) |
| `setBodyPadding(p)` | `int` | Padding inside body area (default 12px) |
| `setShowBorder(v)` | `bool` | Outer rounded border (default `true`) |
| `setShowSeparators(v)` | `bool` | Dividers between panels (default `true`) |
| `setAccentColor(c)` | `Color` | Left bar and active header tint |
| `setTitleFontSize(s)` | `int` | Header title font size |
| `setWidth(w)` | `int` | Fixed width |
| `setFlex(n)` | `int` | Flex factor in parent |

---

### TreeView

Scrollable hierarchical tree with expand/collapse, single selection, keyboard navigation, and optional indent guide lines.

```cpp
TreeNode root("Project");
auto &src = root.addChild(TreeNode("src"));
src.expanded = true;
src.addChild(TreeNode("main.cpp"));

auto tv = TreeView(root)
    ->setOnSelectionChanged([](const TreeNode *n) {
        std::cout << n->label << std::endl;
    })
    ->setShowGuideLines(true)
    ->setFlex(1);

// Multiple roots
auto tv = TreeView({rootA, rootB, rootC});
```

**TreeNode struct**

```cpp
TreeNode node("label", "optional-id");
node.expanded  = true;
node.disabled  = false;
node.icon      = L"\uE8B7";
node.userData  = &myObj;

node.addChild(TreeNode("child"));
node.expandAll();
node.collapseAll();
node.isLeaf();
```

**Methods**

| Method | Type | Description |
|---|---|---|
| `setOnSelectionChanged(fn)` | `void(const TreeNode*)` | Fires on click |
| `setOnNodeExpanded(fn)` | `void(const TreeNode*)` | Fires on expand |
| `setOnNodeCollapsed(fn)` | `void(const TreeNode*)` | Fires on collapse |
| `setOnNodeDoubleClicked(fn)` | `void(const TreeNode*)` | Fires on double-click |
| `setRoots(vector<TreeNode>)` | — | Replace the entire tree at runtime |
| `selectById(id)` | `string` | Select a node by its id field |
| `expandAll()` / `collapseAll()` | — | Expand or collapse all nodes |
| `selectedNode()` | `const TreeNode*` | Currently selected node |
| `setRowHeight(h)` | `int` | Row height in pixels (default 28) |
| `setIndentWidth(w)` | `int` | Pixels per depth level (default 20) |
| `setShowGuideLines(v)` | `bool` | Vertical indent guide lines |
| `setFontSize(s)` | `int` | Label font size |
| `setAccentColor(c)` | `Color` | Selection highlight color |
| `setFlex(n)` | `int` | Flex factor in parent |

> **Keyboard:** `↑/↓` move selection · `←` collapse or jump to parent · `→` expand or move to first child · `Home/End` jump to first/last · `Enter/Space` toggle expand.

---

### DataTable

Virtualised sortable data grid with resizable columns, alternating rows, scrollbars, and optional reactive data binding.

```cpp
std::vector<DataColumn> columns = {
    DataColumn("name",   "Name",   180),
    DataColumn("role",   "Role",   130),
    DataColumn("age",    "Age",     60).setAlign(ColumnAlign::Right),
    DataColumn("salary", "Salary", 110).setAlign(ColumnAlign::Right)
        .setFormatter([](const std::string &v){ return "$" + v + "k"; }),
};

std::vector<DataRow> rows = {
    DataRow("1").set("name","Alice").set("role","Engineer").set("age","29").set("salary","120"),
};

auto table = DataTable(columns, rows)
    ->setAlternateRows(true)
    ->setOnRowSelected([](int idx, const DataRow &row) {
        std::cout << row.get("name") << std::endl;
    });

// Reactive rows
State<std::vector<DataRow>> rowsState(..., app);
auto table = DataTable(columns, rowsState);
```

**DataColumn**

```cpp
DataColumn col("key", "Label", 120);
col.setAlign(ColumnAlign::Right);
col.setSortable(false);
col.setResizable(false);
col.setMinWidth(40);
col.setFormatter([](const std::string &v){ return "$" + v; });
```

**DataRow**

```cpp
DataRow row("optional-id");
row.set("name", "Alice").set("age", "29");
row.get("name");
row.disabled = true;
```

**Methods**

| Method | Type | Description |
|---|---|---|
| `setRows(vector<DataRow>)` | — | Replace rows at runtime |
| `sortBy(key, ascending)` | `string, bool` | Sort programmatically |
| `clearSort()` | — | Remove sort, restore insertion order |
| `selectedIndex()` | `int` | Currently selected visual row index |
| `selectedRow()` | `const DataRow*` | Currently selected row data |
| `setAlternateRows(v)` | `bool` | Alternating row background (default true) |
| `setShowColumnDividers(v)` | `bool` | Vertical column divider lines |
| `setRowHeight(h)` | `int` | Row height in pixels (default 30) |
| `setHeaderHeight(h)` | `int` | Header row height (default 36) |
| `setHeaderBackground(c)` | `Color` | Header background color |
| `setAccentColor(c)` | `Color` | Selection highlight and sort arrow color |
| `setOnRowSelected(fn)` | `void(int, DataRow)` | Fires on single click |
| `setOnRowDoubleClicked(fn)` | `void(int, DataRow)` | Fires on double-click |
| `setOnSortChanged(fn)` | `void(string, bool)` | Fires when sort column changes |
| `setFlex(n)` | `int` | Flex factor in parent |
| `setWidth(w)` / `setHeight(h)` | `int` | Fixed dimensions |

---

# Canvas

The Canvas system gives you a full OpenGL-backed 2D drawing surface embedded anywhere in your widget tree. You subclass `RenderSurface`, draw with the `Canvas2D` API (a familiar HTML5-style interface), and plug it into a `CanvasWidget`. Pan, zoom, scrollbars, mouse/keyboard input, and continuous animation are all built in.

---

## Table of Contents

- [CanvasWidget](#canvaswidget)
- [RenderSurface](#rendersurface)
- [Canvas2D — Drawing API](#canvas2d--drawing-api)
  - [Dimensions](#dimensions)
  - [State Stack](#state-stack)
  - [Transform](#transform)
  - [Fill & Stroke Style](#fill--stroke-style)
  - [Gradients](#gradients)
  - [Solid Primitives](#solid-primitives)
  - [Path API](#path-api)
  - [Clip Rect](#clip-rect)
  - [Images](#images)
  - [Text](#text)
  - [Pixel Access](#pixel-access)
- [Viewport](#viewport)
- [KeyEvent](#keyevent)
- [Example — Animated Triangle](#example--animated-triangle)

---

## CanvasWidget

`CanvasWidget` is the widget you place in your layout. It owns an OpenGL context, manages the viewport, and drives your `RenderSurface` every frame.

### Creating a canvas

```cpp
// Bare canvas — no surface yet, fills available space
auto canvas = Canvas();

// Fixed size
auto canvas = Canvas(800, 600);
```

### Attaching a surface

```cpp
auto surface = canvas->setSurface<MySurface>();
// Returns shared_ptr<MySurface> so you can keep a handle to it
```

### Factory

| Signature | Description |
|---|---|
| `Canvas()` | Bare canvas, defaults to 400 × 300, expands to fill parent |
| `Canvas(w, h)` | Fixed-size canvas |

### Methods

| Method | Returns | Description |
|---|---|---|
| `setSurface<T>(args...)` | `shared_ptr<T>` | Construct and attach a `RenderSurface` subclass. Any constructor arguments for `T` are forwarded. Replaces any previously attached surface. |
| `getSurface()` | `RenderSurface*` | Pointer to the currently active surface, or `nullptr` if none. |
| `setSize(w, h)` | `shared_ptr<CanvasWidget>` | Fix the widget dimensions and disable auto-sizing. |
| `setCanvasSize(w, h)` | `shared_ptr<CanvasWidget>` | Set the logical drawing surface size (used for pan/zoom extents). Defaults to the same as the view size. |
| `setViewportEnabled(bool)` | `shared_ptr<CanvasWidget>` | Enable or disable pan/zoom. Default: `true`. |
| `setScrollbarsEnabled(bool)` | `shared_ptr<CanvasWidget>` | Show fade-in scrollbars when panning. Default: `true`. |
| `viewport()` | `Viewport&` | Direct access to the viewport for programmatic zoom and pan. |
| `redraw()` | `shared_ptr<CanvasWidget>` | Request a repaint on the next frame. |
| `onViewportChanged` | callback | `std::function<void(float zoom)>` — fires whenever zoom or pan changes. |
| `onGLResize` | callback | `std::function<void(int w, int h)>` — fires when the GL surface is resized. |

### Built-in input controls

| Input | Action |
|---|---|
| Middle mouse button drag | Pan |
| Space + left mouse drag | Pan |
| Ctrl + scroll wheel | Zoom toward cursor |
| Shift + scroll wheel | Pan horizontally |
| Scroll wheel | Pan vertically |
| Ctrl + `+` or numpad `+` | Zoom in |
| Ctrl + `-` or numpad `-` | Zoom out |
| Ctrl + `0` | Reset zoom to 1× and center |

Mouse and keyboard events are forwarded to your `RenderSurface` in canvas-space coordinates (after viewport transform).

---

## RenderSurface

Subclass `RenderSurface` to implement your drawing logic. Attach it to a `CanvasWidget` with `setSurface<T>()`.

```cpp
class RenderSurface {
public:
    virtual ~RenderSurface() = default;

    // Called once when the GL context is ready. Set up textures, images, etc.
    virtual void initialize(int canvasWidth, int canvasHeight) = 0;

    // Called when the canvas is resized. Update any size-dependent resources.
    virtual void resize(int newWidth, int newHeight) = 0;

    // Called before the GL context is destroyed. Release all GL resources.
    virtual void destroy() = 0;

    // Called every frame before rendering. Use for animation state, physics, etc.
    // dt is elapsed time in seconds since the last frame.
    virtual void update(double dt) = 0;

    // Optional raw GL pass that runs before Canvas2D begins.
    // Use for FBO rendering, custom shaders, or anything that can't go through Canvas2D.
    virtual void preRender() {}

    // Main drawing entry point. Called every frame inside the GL render pass.
    // ctx is your Canvas2D drawing context for this frame.
    virtual void render(Canvas2D& ctx) = 0;

    // Mouse input — coordinates are in canvas space (accounting for pan/zoom).
    virtual void onMouseDown(float x, float y)       {}
    virtual void onMouseMove(float x, float y)       {}
    virtual void onMouseUp(float x, float y)         {}
    virtual void onRightMouseDown(float x, float y)  {}

    // Keyboard input.
    virtual void onKeyDown(const KeyEvent& e) {}
    virtual void onKeyUp(const KeyEvent& e)   {}

    // Return true to request a new frame every tick (for animations).
    // Return false to only repaint on user input or explicit redraw() calls.
    virtual bool needsContinuousRedraw() const { return false; }
};
```

### Lifecycle order

```
GL context ready → initialize()
                         ↓
           every frame → update(dt)
                       → preRender()    ← raw GL, optional
                       → render(ctx)    ← Canvas2D drawing
                         ↓
GL context going away → destroy()
```

### Minimal example

```cpp
class MyPainter : public RenderSurface {
public:
    void initialize(int w, int h) override {}
    void resize(int w, int h)     override {}
    void destroy()                override {}
    void update(double dt)        override {}

    void render(Canvas2D& ctx) override {
        ctx.setFillColor({30, 30, 30, 255});
        ctx.fillRect(0, 0, ctx.width(), ctx.height());

        ctx.setFillColor({255, 128, 0, 255});
        ctx.fillCircle(ctx.width() / 2.f, ctx.height() / 2.f, 80.f);
    }
};
```

---

## Canvas2D — Drawing API

A `Canvas2D` instance is handed to you inside `render()` every frame. It is modelled closely after the HTML5 Canvas 2D API, so if you know that, you already know most of this.

> **Important:** Do not construct `Canvas2D` yourself. Only use the instance passed to `render()`.

---

### Dimensions

```cpp
int ctx.width()    // current canvas width in pixels
int ctx.height()   // current canvas height in pixels
```

---

### State Stack

Saves and restores the complete drawing state: transform, fill color, stroke color, line width, global alpha, gradient state, clip depth, and text settings.

```cpp
ctx.save();     // push state
ctx.restore();  // pop state
```

---

### Transform

Transforms stack on top of each other and apply to all subsequent drawing. Use `save()`/`restore()` to limit their scope.

```cpp
ctx.translate(dx, dy);          // move origin
ctx.scale(sx, sy);              // scale from origin
ctx.rotate(angleInRadians);     // rotate clockwise
ctx.resetTransform();           // clear all transforms
```

---

### Fill & Stroke Style

```cpp
ctx.setFillColor(color);        // color used by fill operations
ctx.setStrokeColor(color);      // color used by stroke operations
ctx.setLineWidth(pixels);       // stroke line thickness
ctx.setGlobalAlpha(alpha);      // 0.0 = invisible, 1.0 = fully opaque
ctx.setLineCap(cap);            // LineCap::Butt | Round | Square
ctx.setLineJoin(join);          // LineJoin::Miter | Round | Bevel
ctx.setMiterLimit(limit);       // miter join limit
ctx.setFillRule(rule);          // FillRule::NonZero | EvenOdd
ctx.setCompositeOp(op);         // CompositeOp::SourceOver | Copy | Xor | Multiply | Screen
```

---

### Gradients

Gradients replace the fill color. Call `beginLinearGradient()` or `beginRadialGradient()`, add color stops, then call `setFillGradient()` before any fill operation.

#### Linear gradient

```cpp
ctx.beginLinearGradient(x0, y0, x1, y1);   // start point → end point
ctx.addColorStop(0.0f, colorA);
ctx.addColorStop(0.5f, colorB);
ctx.addColorStop(1.0f, colorC);
ctx.setFillGradient();

ctx.fillRect(x, y, w, h);   // drawn with the gradient
```

#### Radial gradient

```cpp
ctx.beginRadialGradient(cx, cy, innerRadius, outerRadius);
ctx.addColorStop(0.0f, innerColor);
ctx.addColorStop(1.0f, outerColor);
ctx.setFillGradient();

ctx.fillCircle(cx, cy, outerRadius);
```

A gradient is active until you call `setFillColor()` again, which cancels it.

---

### Solid Primitives

These do not require a path — they draw immediately.

```cpp
// Rectangle — clear to transparent
ctx.clearRect(x, y, width, height);

// Rectangle — filled
ctx.fillRect(x, y, width, height);

// Rectangle — stroked outline only
ctx.strokeRect(x, y, width, height);

// Rounded rectangle — filled
ctx.fillRoundedRect(x, y, width, height, cornerRadius);

// Rounded rectangle — stroked outline only
ctx.strokeRoundedRect(x, y, width, height, cornerRadius);

// Circle — filled
ctx.fillCircle(centerX, centerY, radius);

// Circle — stroked outline only
ctx.strokeCircle(centerX, centerY, radius);
```

---

### Path API

Paths let you describe arbitrary shapes before filling or stroking them. A path accumulates points until you call `fill()` or `stroke()`.

```cpp
ctx.beginPath();                          // start a new path (clears previous)

ctx.moveTo(x, y);                         // lift pen and move to point
ctx.lineTo(x, y);                         // draw line from current point
ctx.arc(cx, cy, r, startAngle, endAngle, anticlockwise);  // arc / full circle
ctx.arcTo(x1, y1, x2, y2, radius);       // arc tangent to two lines
ctx.quadraticCurveTo(cpx, cpy, x, y);    // quadratic Bézier
ctx.bezierCurveTo(cp1x, cp1y, cp2x, cp2y, x, y);  // cubic Bézier
ctx.rect(x, y, width, height);           // add rectangle sub-path
ctx.ellipse(cx, cy, rx, ry, rotation, startAngle, endAngle, anticlockwise);
ctx.closePath();                          // line back to the start of this sub-path

ctx.fill();     // fill the accumulated path using the current fill color / gradient
ctx.stroke();   // stroke the accumulated path using the current stroke color
ctx.clip();     // use pushClipRect/popClipRect for scissor clipping instead
```

**Angle convention:** `0` radians = 3 o'clock, angles increase clockwise. Same as HTML5 Canvas.

---

### Clip Rect

Restricts all drawing to a rectangular region using the GPU scissor test. Clips nest correctly with `save()`/`restore()`.

```cpp
ctx.pushClipRect(x, y, width, height);

// Everything drawn here is clipped to that rectangle
ctx.fillRect(...);
ctx.fillText(...);

ctx.popClipRect();   // restore previous clip
```

---

### Images

Load images inside `initialize()` or `update()`, never inside `render()`.

#### Loading

```cpp
// From a file path
Canvas2DImage* img = ctx.loadImage("assets/photo.png");

// From a byte buffer in memory (PNG, JPG, etc.)
Canvas2DImage* img = ctx.loadImageFromMemory(dataPtr, byteLength);

// Wrap an existing GL texture you already own
// FluxUI will not delete this texture when you call freeImage()
Canvas2DImage* img = ctx.wrapTexture(glTexId, width, height);
```

#### Updating pixels

```cpp
// Replace the pixel data of an existing image in-place.
// rgba must be width * height * 4 bytes (RGBA, 8 bits per channel).
ctx.updateTexture(img, rgbaPtr, newWidth, newHeight);
```

#### Drawing

```cpp
// Draw at natural size at (dx, dy)
ctx.drawImage(img, dx, dy);

// Draw scaled to fill (dx, dy, dw, dh)
ctx.drawImage(img, dx, dy, destWidth, destHeight);

// Source crop + destination rect
ctx.drawImage(img, srcX, srcY, srcWidth, srcHeight,
                   destX, destY, destWidth, destHeight);
```

#### Freeing

```cpp
ctx.freeImage(img);   // releases the GL texture and deletes the object
img = nullptr;        // pointer is now dangling
```

---

### Text

#### Registering fonts

Fonts must be registered once before use, typically at the top of `initialize()`:

```cpp
// Windows
Canvas2D::registerFont(canvasGL, "sans",             "C:/Windows/Fonts/segoeui.ttf");
Canvas2D::registerFont(canvasGL, "sans-bold",        "C:/Windows/Fonts/segoeuib.ttf");
Canvas2D::registerFont(canvasGL, "sans-italic",      "C:/Windows/Fonts/segoeuii.ttf");
Canvas2D::registerFont(canvasGL, "sans-bold-italic", "C:/Windows/Fonts/segoeuiz.ttf");
Canvas2D::registerFont(canvasGL, "mono",             "C:/Windows/Fonts/consola.ttf");

// Linux / Android — provide your own TTF paths
Canvas2D::registerFont(canvasGL, "sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
```

`canvasGL` is the `Canvas2DGL*` pointer. You can access it via `canvas->canvasGL_` after `initialize()` is called.

#### Font descriptor format

```
[bold] [italic] <size>px <family>

Examples:
  "16px sans"
  "bold 24px sans"
  "italic 14px mono"
  "bold italic 20px sans"
```

#### Drawing text

```cpp
ctx.setFont("bold 18px sans");

ctx.setTextAlign(CanvasTextAlign::Left);    // Left · Center · Right
ctx.setTextBaseline(TextBaseline::Top);     // Top · Middle · Bottom · Alphabetic

ctx.setFillColor({255, 255, 255, 255});
ctx.fillText("Hello, world!", x, y);

ctx.setStrokeColor({0, 0, 0, 255});
ctx.strokeText("Outlined", x, y);          // draws text with a 1px outline effect

float textWidth = ctx.measureText("Hello, world!");
```

**Baseline reference:**

| Value | Y origin |
|---|---|
| `Alphabetic` | Baseline of lowercase letters (default, matches CSS) |
| `Top` | Top of the em box |
| `Middle` | Middle of the em box |
| `Bottom` | Bottom of the descender |

---

### Pixel Access

Read pixels from the framebuffer or write raw RGBA data to the canvas.

```cpp
// Read — fills `out` with w*h*4 bytes (RGBA). Y=0 is the top of the region.
std::vector<uint8_t> pixels;
ctx.getImageData(x, y, width, height, pixels);

// Write — blits a raw RGBA buffer at (dx, dy) at the source dimensions.
ctx.putImageData(pixels, sourceWidth, sourceHeight, destX, destY);
```

---

## Viewport

Accessed via `canvas->viewport()`. You can read or write zoom and pan state at any time, including from inside `update()` or event callbacks.

```cpp
Viewport& vp = canvas->viewport();
```

| Method | Returns | Description |
|---|---|---|
| `zoomIn()` | — | Zoom in 1.25× toward view center |
| `zoomOut()` | — | Zoom out 0.8× toward view center |
| `zoomToward(screenX, screenY, factor)` | — | Zoom by `factor` toward a screen-space point |
| `resetZoom()` | — | Set zoom to 1× and center the canvas |
| `fitToView()` | — | Scale and center so the full canvas is visible |
| `panByScreen(dx, dy)` | — | Pan by pixel deltas in screen space |
| `setOffset(canvasX, canvasY)` | — | Set the pan offset directly in canvas space |
| `screenToCanvas(sx, sy)` | `pair<float, float>` | Convert a screen coordinate to canvas space |
| `zoom()` | `float` | Current zoom factor |
| `offsetX()` | `float` | Current horizontal pan offset (canvas space) |
| `offsetY()` | `float` | Current vertical pan offset (canvas space) |
| `viewW()` / `viewH()` | `float` | Viewport dimensions in pixels |
| `canvasW()` / `canvasH()` | `float` | Canvas dimensions in pixels |

**Zoom range:** 1/16× minimum to 32× maximum, with snapping at common levels (0.25×, 0.5×, 1×, 2×, 4×, 8× …).

---

## KeyEvent

Passed to `onKeyDown()` and `onKeyUp()`.

```cpp
struct KeyEvent {
    int  codepoint;    // Unicode character (printable chars), or 0
    int  virtualKey;   // Platform-normalized virtual key code
    bool ctrl;
    bool shift;
    bool alt;
};
```

---

## Example — Animated Triangle

A complete, runnable app showing a gradient triangle that rotates its hue over time.

```cpp
#include "flux/flux.hpp"
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// Surface
// ─────────────────────────────────────────────────────────────────────────────

class TriangleSurface : public RenderSurface {
    float time_ = 0.f;

public:
    void initialize(int, int) override {}
    void resize(int, int)     override {}
    void destroy()            override {}

    void update(double dt) override {
        time_ += float(dt);
    }

    void render(Canvas2D& ctx) override {
        float w  = float(ctx.width());
        float h  = float(ctx.height());
        float cx = w * 0.5f;
        float cy = h * 0.5f;
        float r  = std::min(w, h) * 0.4f;

        // Dark background
        ctx.setFillColor({15, 15, 20, 255});
        ctx.fillRect(0, 0, w, h);

        // Animated gradient — hue shifts over time
        float hue = std::fmod(time_ * 30.f, 360.f);
        ctx.beginLinearGradient(cx, cy - r, cx, cy + r);
        ctx.addColorStop(0.f, Color::fromHSV(hue,         0.8f, 1.0f));
        ctx.addColorStop(1.f, Color::fromHSV(hue + 120.f, 0.8f, 0.6f));
        ctx.setFillGradient();

        // Equilateral triangle
        ctx.beginPath();
        ctx.moveTo(cx,               cy - r);
        ctx.lineTo(cx - r * 0.866f,  cy + r * 0.5f);
        ctx.lineTo(cx + r * 0.866f,  cy + r * 0.5f);
        ctx.closePath();
        ctx.fill();

        // White label
        ctx.setFillColor({255, 255, 255, 200});
        ctx.setFont("bold 16px sans");
        ctx.setTextAlign(CanvasTextAlign::Center);
        ctx.setTextBaseline(TextBaseline::Top);
        ctx.fillText("FluxUI Canvas", cx, cy + r + 16.f);
    }

    // Return true → repaint every frame (drives the animation)
    bool needsContinuousRedraw() const override { return true; }
};

// ─────────────────────────────────────────────────────────────────────────────
// App widget
// ─────────────────────────────────────────────────────────────────────────────

class MyApp : public Widget {
public:
    WidgetPtr build() override {
        auto canvas = Canvas(512, 512);
        canvas->setScrollbarsEnabled(false);
        canvas->setViewportEnabled(false);
        canvas->setSurface<TriangleSurface>();

        return Scaffold(
            AppBar("Animated Triangle"),
            Center(canvas)
        );
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Entry point
// ─────────────────────────────────────────────────────────────────────────────

WidgetPtr createApp(FluxUI* app) {
    return FluxApp(
        "Triangle",
        std::make_shared<MyApp>(),
        AppTheme::dark(),
        false,   // debugShowWidgetBounds
        560,     // window width
        620,     // window height
        false,   // maximize
        false    // fullscreen
    );
}
```

---

## Quick Reference

```
CanvasWidget
├── setSurface<T>()            attach your RenderSurface
├── setViewportEnabled()       pan & zoom via mouse/keyboard
├── setScrollbarsEnabled()     fade-in scrollbars
├── viewport()                 programmatic zoom/pan
└── redraw()                   request a repaint

RenderSurface  (your subclass)
├── initialize(w, h)           GL ready — load textures here
├── update(dt)                 per-frame logic
├── preRender()                raw GL pass (optional)
├── render(ctx)                Canvas2D drawing
├── resize(w, h)               canvas resized
├── destroy()                  cleanup before GL teardown
├── onMouseDown/Move/Up(x, y)  canvas-space mouse input
├── onKeyDown/Up(event)        keyboard input
└── needsContinuousRedraw()    return true for animation

Canvas2D  (inside render())
├── save() / restore()
├── translate / scale / rotate / resetTransform
├── setFillColor / setStrokeColor / setLineWidth / setGlobalAlpha
├── beginLinearGradient / beginRadialGradient / addColorStop / setFillGradient
├── fillRect / strokeRect / fillRoundedRect / fillCircle …
├── beginPath / moveTo / lineTo / arc / bezierCurveTo / fill / stroke …
├── pushClipRect / popClipRect
├── loadImage / drawImage / updateTexture / freeImage
├── registerFont / setFont / fillText / measureText
└── getImageData / putImageData
```
---

## State

### Conditional

Ternary-style conditional rendering.

```cpp
Conditional(isLoggedIn)
    ->Then([]{ return Dashboard(); })
    ->Else([]{ return LoginPage(); });

Conditional(itemCount, [](int v){ return v > 0; })
    ->Then([]{ return ItemList(); })
    ->Else([]{ return EmptyState(); });
```

| Method | Description |
|---|---|
| `Then(builder)` | Widget when condition is true |
| `Else(builder)` | Widget when condition is false |

---

### Switch

C++-style switch-case conditional rendering.

```cpp
Switch(tabIndex)
    ->Case(0, []{ return HomePage(); })
    ->Case(1, []{ return ProfilePage(); })
    ->Default([]{ return ErrorPage(); });
```

| Method | Description |
|---|---|
| `Case(value, builder)` | Widget when state equals value |
| `Default(builder)` | Fallback when no case matches |

---

## Layout

### Row

Lays children out horizontally with flex expansion support.

```cpp
Row({
    Text("Label"),
    Expanded(TextInput()),
    Button("Send")
})->setSpacing(8)->setCrossAxisAlignment(CrossAxisAlignment::Center);
```

| Method | Type | Description |
|---|---|---|
| `setSpacing(px)` | `int` | Gap between children |
| `setCrossAxisAlignment(a)` | `CrossAxisAlignment` | Vertical alignment |
| `setMainAxisAlignment(a)` | `MainAxisAlignment` | Horizontal distribution |
| `setMainAxisSize(s)` | `MainAxisSize` | `Max` (fill) or `Min` (shrink-wrap) |
| `setWidth(w)` | `int` | Fixed width |
| `setHeight(h)` | `int` | Fixed height |
| `setPadding(p)` | `int` | Uniform padding |
| `setBackgroundColor(c)` | `Color` | Row background |
| `setFlex(n)` | `int` | Flex factor in parent |

---

### Column

Lays children out vertically with flex expansion support.

```cpp
Column({
    AppBar("Title"),
    Expanded(ListView(itemsState)->itemBuilder(...)),
})->setSpacing(0);
```

| Method | Type | Description |
|---|---|---|
| `setSpacing(px)` | `int` | Gap between children |
| `setCrossAxisAlignment(a)` | `CrossAxisAlignment` | Horizontal alignment |
| `setMainAxisAlignment(a)` | `MainAxisAlignment` | Vertical distribution |
| `setMainAxisSize(s)` | `MainAxisSize` | `Max` (fill) or `Min` (shrink-wrap) |
| `setWidth(w)` | `int` | Fixed width |
| `setHeight(h)` | `int` | Fixed height |
| `setPadding(p)` | `int` | Uniform padding |
| `setBackgroundColor(c)` | `Color` | Column background |
| `setBorderRadius(r)` | `int` | Corner rounding |
| `setMinWidth(w)` | `int` | Minimum width |
| `setFlex(n)` | `int` | Flex factor in parent |

---

### Stack

Layers children on top of each other. Supports absolute positioning via margins.

```cpp
Stack(
    AssetImage("bg.jpg")->setWidth(400)->setHeight(300),
    Positioned(Text("Overlay"), 10, 10)
)->setExpand(true);
```

**Factory:** `Stack(widgets...)` or `Stack({widget, widget, ...})`

**Positioned helper**

```cpp
Positioned(child, left, top, right, bottom)
Positioned(child, state, xTransform, yTransform)
Positioned(child, xState, xTransform, yState, yTransform)
```

**StackWidget methods**

| Method | Description |
|---|---|
| `setAlignment(a)` | Default alignment for unpositioned children |
| `setExpand(bool)` | Fill available space instead of shrink-wrapping |
| `setWidth(w)` / `setHeight(h)` | Fixed dimensions |
| `setPadding(p)` | Uniform padding |
| `setBackgroundColor(c)` | Background fill |
| `setBorderRadius(r)` | Corner rounding |
| `setFlex(n)` | Flex factor in parent |

---

### Container

Single-child box with full styling — background, border, radius, padding, margin, size constraints, hover effects.

```cpp
Container(Text("Hello"))
    ->setBackgroundColor(RGB(240,248,255))
    ->setBorderColor(RGB(33,150,243))
    ->setBorderWidth(1)
    ->setBorderRadius(8)
    ->setPadding(16)
    ->setHoverBackgroundColor(RGB(220,240,255));

// Reactive background
Container(child)->setBackgroundColor(selectedState,
    [](bool v){ return v ? RGB(230,245,255) : RGB(255,255,255); });

// Reactive visibility
Container(child)->setVisible(visibleState, [](bool v){ return v; });
```

| Method | Type | Description |
|---|---|---|
| `setBackgroundColor(color)` | `Color` | Fill color |
| `setBackgroundColor(State, transform)` | State | Reactive background |
| `setHoverBackgroundColor(color)` | `Color` | Fill on hover |
| `setBorderColor(color)` | `Color` | Border stroke |
| `setBorderColor(State, transform)` | State | Reactive border color |
| `setHoverBorderColor(color)` | `Color` | Border on hover |
| `setBorderWidth(w)` | `int` or State | Border thickness |
| `setBorderRadius(r)` | `int` or State | Corner rounding |
| `setPadding(p)` | `int` | Uniform inner padding |
| `setPaddingAll(l,t,r,b)` | `int ×4` | Per-side inner padding |
| `setMargin(m)` | `int` | Uniform outer margin |
| `setMarginAll(l,t,r,b)` | `int ×4` | Per-side outer margin |
| `setWidth(w)` | `int` or State | Fixed width |
| `setHeight(h)` | `int` or State | Fixed height |
| `setMinWidth(w)` | `int` | Minimum width |
| `setMinHeight(h)` | `int` | Minimum height |
| `setMaxWidth(w)` | `int` | Maximum width |
| `setMaxHeight(h)` | `int` | Maximum height |
| `setFlex(n)` | `int` | Flex factor |
| `setOnHover(fn)` | `void(bool)` | Hover enter/leave callback |
| `setVisible(v)` | `bool` or State | Show/hide |

---

### Center

Centers its single child both horizontally and vertically.

```cpp
Center(Text("No items found")->setTextColor(RGB(150,150,150)));
```

---

### Expanded

Causes its child to fill remaining space along the parent Row or Column's main axis.

```cpp
Row({
    Text("Label"),
    Expanded(TextInput()),
    Button("Go")
});

// Proportional — 2:1 split
Row({
    Expanded(Column(...), 2),
    Expanded(Column(...), 1)
});
```

**Factory:** `Expanded(child, flex = 1)`

**Methods**

| Method | Description |
|---|---|
| `setFlex(n)` | Override flex factor |
| `setPadding(p)` | Uniform inner padding |
| `setBackgroundColor(c)` | Background fill |

---

### SizedBox

Fixed-size box. Used as a spacer or to constrain a child to an exact size.

```cpp
SizedBox(0, 24);                        // 24px vertical gap
SizedBox(16, 0);                        // 16px horizontal gap
SizedBox(200, 48, Button("Submit"));    // constrained child
```

---

### Padding

Uniform padding wrapper. Shorthand for `Container` with a single padding value.

```cpp
Padding(16, Text("Padded content"));
```

---

### SplitView

Two-pane resizable container with a draggable divider.

```cpp
SplitView(leftWidget, rightWidget, 0.3f)
    ->setMinPaneWidth(120)
    ->setDividerColor(RGB(210, 210, 210));

SplitViewVertical(topWidget, bottomWidget, 0.4f);

State<float> ratio(0.5f, app);
SplitView(left, right)->setRatio(ratio);
```

**Methods**

| Method | Type | Description |
|---|---|---|
| `setRatio(r)` | `float` 0–1 | Fraction of space given to pane 0 |
| `setRatio(State<float>)` | State | Reactive ratio binding |
| `setMinPaneWidth(px)` | `int` | Minimum size of either pane |
| `setDividerWidth(px)` | `int` | Divider thickness (default 6px) |
| `setDividerColor(c)` | `Color` | Default divider color |
| `setDividerHoverColor(c)` | `Color` | Divider color on hover |
| `setDividerDragColor(c)` | `Color` | Divider color while dragging |
| `setVertical(v)` | `bool` | Switch to top/bottom split |
| `setResizable(v)` | `bool` | Allow drag resize (default true) |
| `setOnRatioChanged(fn)` | `void(float)` | Fires after drag completes |
| `getRatio()` | `float` | Current ratio |
| `swapPanes()` | — | Swap pane 0 and pane 1 |
| `collapsePane(idx)` | `int` | Collapse pane 0 or 1 fully |

---

## Structure

### Scaffold

Root structure widget. Manages the overlay stack used by Dropdown, Tooltip, Dialog, and ContextMenu.

```cpp
Scaffold(
    AppBar("My App"),
    Column({ Text("Content") })
);
```

> **Overlay zIndex order:** Tooltip = 50, Dropdown = 100, ContextMenu = 150, Dialog = 200.

| Method | Description |
|---|---|
| `addOverlay(widget, renderer, zIndex)` | Register a floating overlay |
| `removeOverlay(widget)` | Unregister a floating overlay |
| `clearOverlays()` | Remove all overlays |
| `hasOverlays()` | Returns true if overlays are active |
| `getTopmostOverlay()` | Returns topmost overlay widget pointer |

---

### AppBar

56px tall header bar with blue background and bold white title.

```cpp
AppBar("Dashboard");
```

---

### Card

White rounded-corner box with light border and 16px padding.

```cpp
Card(
    Column({
        Text("Title")->setFontWeight(FontWeight::Bold),
        Text("Description")
    })->setSpacing(8)
);
```

---

## Navigation

### TabView

Tab bar with swappable content panes.

```cpp
TabView({
    Tab("General",  generalWidget),
    Tab("Display",  displayWidget),
    Tab("Network",  networkWidget),
})
->setOnTabChanged([](int i) { std::cout << "Tab: " << i << std::endl; });

State<int> activeTab(0, app);
TabView({...})->setActiveIndex(activeTab);
```

**Methods**

| Method | Type | Description |
|---|---|---|
| `setActiveIndex(idx)` | `int` | Switch to tab by index |
| `setActiveIndex(State<int>)` | State | Two-way reactive binding |
| `setOnTabChanged(fn)` | `void(int)` | Fires when active tab changes |
| `setTabBarHeight(h)` | `int` | Height of the tab bar (default 40px) |
| `setTabMinWidth(w)` | `int` | Minimum tab button width (default 90px) |
| `setTabFontSize(s)` | `int` | Tab label font size |
| `setIndicatorColor(c)` | `Color` | Active tab underline color |
| `setActiveTabText(c)` | `Color` | Active tab label color |
| `setBarBackground(c)` | `Color` | Tab bar background color |
| `setContentPadding(p)` | `int` | Padding inside the content area |
| `setHasContentBorder(v)` | `bool` | Border around content pane |
| `setAccentColor(c)` | `Color` | Sets indicator, active text, hover together |
| `setTabContent(idx, widget)` | — | Replace a tab's content at runtime |
| `setTabLabel(idx, label)` | — | Rename a tab at runtime |
| `tabCount()` | `int` | Number of tabs |
| `setFlex(n)` | `int` | Flex factor in parent |

---

### MenuBar

Horizontal strip of labeled menus with pulldown lists.

```cpp
auto menuBar = MenuBar({
    MenuBarItem("File", {
        ContextMenuItem::Action("New",  [&]{ newFile(); }),
        ContextMenuItem::Separator(),
        ContextMenuItem::Action("Exit", []{ PostQuitMessage(0); }),
    }),
    MenuBarItem("Edit", {
        ContextMenuItem::Action("Cut",   [&]{ cut(); }),
        ContextMenuItem::Action("Copy",  [&]{ copy(); }),
    }),
});
```

**Methods**

| Method | Type | Description |
|---|---|---|
| `setBarHeight(h)` | `int` | Height of the menu bar strip (default 28px) |
| `setBarBackground(c)` | `Color` | Bar background color |
| `setItemHeight(h)` | `int` | Dropdown item row height (default 28px) |
| `setMinMenuWidth(w)` | `int` | Minimum dropdown width (default 160px) |

---

## Overlay

### Dropdown

Select input that opens a scrollable overlay list.

```cpp
Dropdown({"Nepal", "India", "USA", "UK"})
    ->setPlaceholder("Select a country")
    ->setSelectedValue(countryState)
    ->setOnSelectionChanged([&](int i, const std::string& v){
        handleSelect(v);
    });
```

**Factory:** `Dropdown(options)` or `Dropdown()`

**Methods**

| Method | Type | Description |
|---|---|---|
| `setOptions(opts)` | `vector<string>` | Set or replace options |
| `setPlaceholder(text)` | `string` | Text when nothing selected |
| `setSelectedIndex(State<int>)` | State | Two-way binding by index |
| `setSelectedValue(State<string>)` | State | Two-way binding by value |
| `setOnSelectionChanged(fn)` | `void(int, string)` | Change callback |
| `setItemHeight(h)` | `int` | Row height (default 32px) |
| `setMaxVisibleItems(n)` | `int` | Max rows before scroll (default 6) |
| `setWidth(w)` | `int` | Fixed width |

---

### Toast

Floating notification toasts anchored to any point in the tree.

```cpp
auto toast = Toast()
    ->setPosition(ToastPosition::BottomRight)
    ->setMaxVisible(3);

toast->show("File saved",      ToastType::Success);
toast->show("Connection lost", ToastType::Error);

toast->showEntry({
    .message     = "Upload failed",
    .title       = "Network Error",
    .type        = ToastType::Error,
    .durationMs  = 0,
    .actionLabel = "Retry",
    .onAction    = [&]{ retry(); },
});
```

**ToastPosition / ToastType** — same as before (BottomRight/Left/Center, TopRight/Left/Center; Info/Success/Warning/Error).

**Methods**

| Method | Type | Description |
|---|---|---|
| `show(message, type, durationMs)` | — | Quick fire with defaults |
| `showEntry(ToastEntry)` | — | Full control over all fields |
| `dismissTop()` | — | Dismiss the topmost visible toast |
| `dismissAll()` | — | Dismiss all toasts |
| `setPosition(pos)` | `ToastPosition` | Screen corner/edge |
| `setMaxVisible(n)` | `int` | Max simultaneous toasts (default 3) |
| `setToastWidth(w)` | `int` | Toast panel width |
| `setToastHeight(h)` | `int` | Base height per toast |
| `setMarginEdge(m)` | `int` | Distance from window edge |
| `setSpacing(s)` | `int` | Gap between stacked toasts |
| `setFontSize(s)` | `int` | Toast text size |
| `setBgColor(c)` | `Color` | Toast background |
| `setColors(info, success, warning, error)` | `Color ×4` | Override all accent colors |

---

### Tooltip

Shows a floating text bubble on hover.

```cpp
Tooltip(
    Button("Delete", [&]{ deleteItem(); }),
    "Permanently removes the item"
)->setPosition(TooltipPosition::Above);
```

**Factory:** `Tooltip(anchor, text)`

**Methods**

| Method | Type | Description |
|---|---|---|
| `setTooltipText(text)` | `string` | Update tooltip content |
| `setPosition(pos)` | `TooltipPosition` | `Above · Below · Auto` |
| `setTooltipBackground(color)` | `Color` | Bubble background |
| `setTooltipTextColor(color)` | `Color` | Bubble text color |
| `setTooltipFontSize(size)` | `int` | Font size inside bubble |
| `setTooltipMaxWidth(w)` | `int` | Max bubble width (default 240px) |

---

### Dialog

Modal overlay with semi-transparent backdrop and centered content panel.

```cpp
auto dlg = Dialog(
    Column({
        Text("Confirm delete?")->setFontSize(16),
        Row({
            Button("Cancel", [&]{ dlg->close(); }),
            Button("Delete", [&]{ deleteItem(); dlg->close(); })
        })->setSpacing(8)
    })
)->setSize(360, 160)
 ->setCloseOnClickOutside(true);

dlg->open();
```

**Methods**

| Method | Type | Description |
|---|---|---|
| `open()` | — | Show the dialog |
| `close()` | — | Dismiss the dialog |
| `setContent(widget)` | `WidgetPtr` | Set or replace content |
| `setSize(w, h)` | `int, int` | Panel dimensions (default 400×300) |
| `setCloseOnClickOutside(bool)` | `bool` | Close on backdrop click (default true) |
| `setOverlayAlpha(alpha)` | `int` 0–255 | Backdrop opacity (default 128) |
| `setOnClose(fn)` | `void()` | Called when dismissed |

---

### ContextMenu

Attaches a right-click context menu to any anchor widget.

```cpp
ContextMenu(
    Text("Right click me"),
    {
        {"Cut",   [&]{ cut(); }},
        {"Copy",  [&]{ copy(); }},
        ContextMenuItem::Separator(),
        {"Paste", [&]{ paste(); }, false}
    }
);
```

**ContextMenuItem**

| Factory | Description |
|---|---|
| `ContextMenuItem(label, action, enabled)` | Action item |
| `ContextMenuItem::Action(label, action, enabled)` | Explicit action factory |
| `ContextMenuItem::Separator()` | Visual divider |

**Methods**

| Method | Type | Description |
|---|---|---|
| `setMenuItems(items)` | `vector<ContextMenuItem>` | Replace all items |
| `setItemHeight(h)` | `int` | Row height (default 28px) |
| `setMinWidth(w)` | `int` | Minimum menu width (default 160px) |
| `setMenuBackground(color)` | `Color` | Menu background |
| `setMenuBorder(color)` | `Color` | Menu border color |
| `setItemHoverColor(color)` | `Color` | Row highlight on hover |

---

## Media

### AudioPlayer

Drop-in audio player widget. Supports local files, HTTP/HTTPS URLs, and in-memory buffers.

```cpp
// Local file
AudioPlayer("audio/sample.mp3")->setWidth(380);

// Explicit path setter
AudioPlayer()->setPath("audio/sample.mp3")->setWidth(380);

// Stream from URL
AudioPlayerFromUrl("https://example.com/music.mp3")->setWidth(400);

// In-memory buffer
AudioPlayerFromMemory(myBytes)->setWidth(400);

// With artwork
AudioPlayer("audio/track.mp3")
    ->setArtwork(AssetImage("covers/album.jpg"), 60)
    ->setWidth(420);
```

**Factory**

| Signature | Description |
|---|---|
| `AudioPlayer(path = "")` | Player for a local file path |
| `AudioPlayerFromUrl(url)` | Player that streams from an HTTP/HTTPS URL |
| `AudioPlayerFromMemory(bytes)` | Player backed by a `vector<uint8_t>` buffer |
| `AudioPlayerFromMemory(ptr, len)` | Player backed by a raw pointer + length |

**Methods**

| Method | Type | Description |
|---|---|---|
| `setPath(p)` | `string` | Load a local file path |
| `setUrl(url)` | `string` | Stream from HTTP/HTTPS URL (downloaded on background thread) |
| `setMemory(bytes)` | `vector<uint8_t>` | Play from in-memory buffer (copy overload) |
| `setMemory(ptr, len)` | `uint8_t*, size_t` | Play from raw pointer + length |
| `setArtwork(img, size)` | `ImageWidgetPtr, int` | Attach an artwork thumbnail; `size` defaults to player height |
| `setArtworkSize(px)` | `int` | Resize the artwork column |
| `setOnDotsClicked(fn)` | `void()` | Callback for the three-dot menu button |
| `setWidth(w)` | `int` | Fixed width |

---

### VideoPlayer

Self-contained video player widget. Blits decoded frames each render tick and overlays a control bar on hover. Supports local files, HTTP/HTTPS URLs, and in-memory buffers.

**Platform support:** Android (NanoVG/OES), Windows (GDI StretchDIBits), Linux (Cairo/SDL2).

```cpp
// Local file
VideoPlayer("video/sample.mp4")
    ->setWidth(480)
    ->setHeight(270)
    ->setAutoPlay(true);

// Stream from URL
VideoPlayerFromUrl("https://example.com/video.mp4")
    ->setWidth(480)->setHeight(270);

// In-memory buffer
VideoPlayerFromMemory(bytes)->setWidth(480)->setHeight(270);
```

**Factory**

| Signature | Description |
|---|---|
| `VideoPlayer(path = "")` | Player for a local file path |
| `VideoPlayerFromUrl(url)` | Player that streams from an HTTP/HTTPS URL |
| `VideoPlayerFromMemory(bytes)` | Player backed by a `vector<uint8_t>` buffer |
| `VideoPlayerFromMemory(ptr, len)` | Player backed by a raw pointer + length |

**Methods**

| Method | Type | Description |
|---|---|---|
| `setPath(p)` | `string` | Load a local video file |
| `setUrl(url)` | `string` | Stream from HTTP/HTTPS URL |
| `setMemory(bytes)` | `vector<uint8_t>` | Play from in-memory buffer (copy overload) |
| `setMemory(ptr, len)` | `uint8_t*, size_t` | Play from raw pointer + length |
| `setWidth(w)` | `int` | Fixed width |
| `setHeight(h)` | `int` | Fixed height |
| `setAutoPlay(b)` | `bool` | Start playing immediately on open |

> **Controls:** Click anywhere on the video area to toggle play/pause and show the control bar. The bar auto-hides after 3 seconds of inactivity. Drag the seek thumb to scrub.

---

### CameraView

Fixed-size camera viewfinder with shutter, flash toggle, and camera flip controls.

```cpp
#include "flux/flux_camera_widget.hpp"

CameraView()
    ->setWidth(380)
    ->setHeight(270)
    ->setOnPhoto([](const std::string& path) {
        std::cout << "Saved: " << path << std::endl;
    });
```

**Methods**

| Method | Type | Description |
|---|---|---|
| `setWidth(w)` | `int` | Fixed width (default 380) |
| `setHeight(h)` | `int` | Fixed height (default 270) |
| `setOnPhoto(fn)` | `void(string)` | Fires with the saved file path after each capture |
| `setStartFront(f)` | `bool` | Start with the front-facing camera (Android only) |

---

### MicRecorder

Microphone recorder widget with a scrolling live waveform and WAV file output.

```cpp
#include "flux/mic_recorder_widget.hpp"

MicRecorder()
    ->setWidth(320)
    ->setHeight(120)
    ->setOnSaved([](const std::string& path) {
        std::cout << "Saved to: " << path << std::endl;
    });
```

**Methods**

| Method | Type | Description |
|---|---|---|
| `setWidth(w)` | `int` | Fixed width (default 320) |
| `setHeight(h)` | `int` | Fixed height (default 120) |
| `setOnSaved(fn)` | `void(string)` | Fires with the WAV file path on stop |

---

## Network

### FutureBuilder / FetchBuilder / JsonBuilder

Flutter-inspired async widget that manages loading, error, and data states for HTTP requests.

```cpp
#include "flux/flux_future_builder.hpp"

// Raw string response
FetchBuilder(
    "https://api.example.com/status",
    [](const AsyncSnapshot<std::string>& snap) -> WidgetPtr {
        if (snap.isLoading()) return Text("Loading...");
        if (snap.hasError())  return Text("Error: " + snap.error);
        return Text(snap.data);
    }
);

// Parsed JSON
JsonBuilder(
    "https://api.example.com/user/1",
    [](const AsyncSnapshot<JsonValue>& snap) -> WidgetPtr {
        if (snap.isLoading()) return Text("Loading...");
        if (snap.hasError())  return Text("Error: " + snap.error);
        return Text(snap.data["name"].getString());
    }
);

// Typed
TypedJsonBuilder<User>(
    "https://api.example.com/user/1",
    [](const JsonValue& j) -> User {
        return { j["name"].getString(), j["age"].getInt() };
    },
    [](const AsyncSnapshot<User>& snap) -> WidgetPtr {
        if (snap.isLoading()) return Text("Loading...");
        if (snap.hasError())  return Text("Error: " + snap.error);
        return Text(snap.data.name);
    }
);
```

**AsyncSnapshot\<T\>**

| Field / Method | Description |
|---|---|
| `state` | `ConnectionState::None · Waiting · Done · Error` |
| `data` | Result value — valid only when `hasData()` is true |
| `error` | Error message — valid only when `hasError()` is true |
| `isNone()` | True before the fetch starts |
| `isLoading()` | True while the request is in flight |
| `hasData()` | True when the request completed successfully |
| `hasError()` | True when the request failed |

**Factory helpers**

| Factory | Response type | Description |
|---|---|---|
| `FetchBuilder(url, builder)` | `string` | Raw HTTP response body |
| `JsonBuilder(url, builder)` | `JsonValue` | Parsed JSON value |
| `TypedJsonBuilder<T>(url, mapper, builder)` | `T` | Deserialized struct via a mapper function |

**FutureBuilderWidget methods**

| Method | Description |
|---|---|
| `setBuilder(fn)` | Set the builder callback |
| `setFetcher(fn)` | Set a custom async fetcher instead of HTTP |
| `refresh()` | Reset state and re-run the fetch |

---

### StreamBuilder / JsonStreamBuilder / TypedStreamBuilder

WebSocket-powered async widget. Opens a persistent connection and calls `builder()` on every incoming frame so the UI always reflects the latest server push.

```cpp
#include "flux/flux_stream_builder.hpp"

// Raw text frames
StreamBuilder(
    "wss://example.com/feed",
    [](const StreamSnapshot<std::string>& snap) -> WidgetPtr {
        if (snap.isConnecting()) return Text("Connecting...");
        if (snap.hasError())     return Text("Error: " + snap.error);
        if (!snap.hasData())     return Text("Waiting for data...");
        return Text(snap.data);
    }
);

// Auto-parsed JSON
JsonStreamBuilder(
    "wss://example.com/prices",
    [](const StreamSnapshot<JsonValue>& snap) -> WidgetPtr {
        if (snap.isConnecting()) return Text("...");
        if (snap.hasError())     return Text("Error: " + snap.error);
        if (!snap.hasData())     return Text("–");
        return Text(snap.data["price"].getString());
    }
);

// Typed — user-supplied mapper
TypedStreamBuilder<TickerData>(
    "wss://example.com/ticker",
    [](const JsonValue& j) -> TickerData {
        return { j["symbol"].getString(), j["price"].getFloat() };
    },
    [](const StreamSnapshot<TickerData>& snap) -> WidgetPtr {
        if (!snap.hasData()) return Text("–");
        return Text(snap.data.symbol + ": " + std::to_string(snap.data.price));
    }
);
```

**StreamSnapshot\<T\>**

| Field / Method | Description |
|---|---|
| `state` | `StreamState::None · Connecting · Active · Done · Error` |
| `data` | Latest decoded value — valid only when `hasData()` is true |
| `error` | Error message — valid only when `hasError()` is true |
| `isConnecting()` | True during WebSocket handshake |
| `isActive()` | True while connection is open |
| `hasData()` | True once at least one frame has been decoded successfully |
| `isDone()` | True after server closed the connection cleanly |
| `hasError()` | True on connection failure or server error |

**StreamState enum**

| Value | Description |
|---|---|
| `StreamState::None` | Not yet started |
| `StreamState::Connecting` | Socket handshake in progress |
| `StreamState::Active` | Connection open, data may have arrived |
| `StreamState::Done` | Server closed cleanly |
| `StreamState::Error` | Connection failed or server error |

**Factory helpers**

| Factory | Frame type | Description |
|---|---|---|
| `StreamBuilder(url, builder)` | `string` | Raw text frames |
| `JsonStreamBuilder(url, builder)` | `JsonValue` | Auto-parsed JSON on every frame |
| `TypedStreamBuilder<T>(url, mapper, builder)` | `T` | Deserialized struct via a mapper function |

**StreamBuilderWidget methods**

| Method | Description |
|---|---|
| `setBuilder(fn)` | Set or replace the builder callback |
| `setDecoder(fn)` | Set a custom frame decoder `(string, T&) -> bool` |
| `setUrl(url)` | Set the WebSocket URL |
| `sendMessage(msg)` | Send a text frame to the server |
| `reconnect()` | Close and re-open the connection |
| `snapshot()` | Read the current `StreamSnapshot<T>` |

---

## CLI

The **Flux CLI** (`flux`) scaffolds new projects and builds / runs them on each target platform with a single command. Source and releases live at [github.com/HeyItsBablu/flux-cli](https://github.com/HeyItsBablu/flux-cli).

### Prerequisites

| Platform | Requirements |
|---|---|
| All | CMake 3.22+, Git, curl |
| Windows | Visual Studio 2019+ with **Desktop development with C++** workload |
| Linux | `sudo apt install build-essential cmake git curl` |

### Installation

**Linux / macOS**
```bash
curl -LO https://github.com/HeyItsBablu/flux-cli/releases/latest/download/flux
chmod +x flux
sudo mv flux /usr/local/bin/
```

**Windows**

1. Download `flux.exe` from [Releases](https://github.com/HeyItsBablu/flux-cli/releases/latest)
2. Move it to a folder, e.g. `C:\tools\flux\flux.exe`
3. Add that folder to your system `PATH`
4. Open a new terminal and run `flux` to verify

### Commands

#### `flux create <name>`

Scaffolds a new app in a folder called `<name>`.

```bash
flux create my_app
cd my_app
```

Generated structure:

```
my_app/
├── main.cpp          ← your entire app lives here
├── flux.json         ← app config (do not edit manually)
├── windows/          ← platform build files (do not edit)
└── linux/            ← platform build files (do not edit)
```

#### `flux run <platform>`

Builds and launches the app for the given platform.

```bash
flux run windows
flux run linux
```

| Platform | What it does |
|---|---|
| `windows` | Locates Visual Studio, runs CMake + MSVC build, launches `build/Release/app.exe` |
| `linux` | Runs CMake + GCC/Clang build, launches `build/app` |
| `android` | *(coming soon)* |

### App config — flux.json

```json
{
    "name": "my_app",
    "package": "com.example.my_app"
}
```

| Field | Description |
|---|---|
| `name` | App name used in build output |
| `package` | Android package identifier |

### Building the CLI from source

```bash
git clone https://github.com/HeyItsBablu/flux-cli
cd flux-cli
cmake -B build -S .
cmake --build build --config Release
```