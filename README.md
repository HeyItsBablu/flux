# React UI Framework - Improved Version

A hardened, production-ready React-like UI framework for Windows with Flexbox support.

## 🎉 What's New in This Version

### Security & Stability Improvements

1. **Safe String Operations**
   - All `strcpy`, `strcat`, and `sprintf` replaced with bounds-checked versions
   - Prevents buffer overflow vulnerabilities
   - Proper null termination guaranteed

2. **Multiple Instance Support**
   - Removed global state dependency
   - Can create up to 8 independent UI instances
   - Each window maintains its own state

3. **Input Validation**
   - HTML parsing validates buffer sizes
   - onClick handlers sanitized for security
   - Format string exploits prevented

4. **Error Handling**
   - Custom error callback support
   - Graceful degradation on parse errors
   - Null pointer checks throughout

5. **Memory Safety**
   - Better cleanup on errors
   - Prevents memory leaks during parsing failures
   - Validated allocation checks

### 🎨 Flexbox Layout System

Complete CSS Flexbox implementation including:

#### Flex Container Properties

- **`display: flex`** - Enable flexbox layout
- **`flex-direction`** - Control main axis direction
  - `row` (default)
  - `column`
  - `row-reverse`
  - `column-reverse`

- **`justify-content`** - Align items on main axis
  - `flex-start` (default)
  - `flex-end`
  - `center`
  - `space-between`
  - `space-around`
  - `space-evenly`

- **`align-items`** - Align items on cross axis
  - `flex-start` (default)
  - `flex-end`
  - `center`
  - `stretch`
  - `baseline`

- **`flex-wrap`** - Control wrapping (basic support)
  - `nowrap` (default)
  - `wrap`
  - `wrap-reverse`

- **`gap`** - Space between flex items (in pixels)

#### Flex Item Properties

- **`flex-grow`** - Grow factor (default: 0)
- **`flex-shrink`** - Shrink factor (default: 1)
- **`flex-basis`** - Base size before growing/shrinking

## 📖 API Reference

### Initialization

```c
ReactUI* ReactUI_Create(HINSTANCE hInstance);
```
Creates a new ReactUI instance. Returns NULL on failure.

### Error Handling

```c
void ReactUI_SetErrorCallback(ReactUI* ui, void (*callback)(const char*));
```
Set a custom error handler for parsing and runtime errors.

Example:
```c
void onError(const char* message) {
    MessageBoxA(NULL, message, "Error", MB_OK | MB_ICONERROR);
}

ReactUI* ui = ReactUI_Create(hInstance);
ReactUI_SetErrorCallback(ui, onError);
```

### State Management

```c
void ReactUI_SetState(ReactUI* ui, const char* name, int value);
int ReactUI_GetState(ReactUI* ui, const char* name);
```

Safe state management with bounds checking. Maximum 16 states per instance.

### Rendering

```c
void ReactUI_Render(ReactUI* ui, const char* html);
```

Parses and renders HTML with inline styles. Supports:
- Standard HTML tags (div, button, etc.)
- Inline CSS styles
- Variable substitution with `{{stateName}}`
- Event handlers (onClick)

### Window Management

```c
HWND ReactUI_CreateWindow(ReactUI* ui, const char* title, int width, int height);
int ReactUI_Run();
void ReactUI_ForceUpdate(ReactUI* ui);
void ReactUI_Destroy(ReactUI* ui);
```

## 🎯 Usage Examples

### Basic Flexbox Row

```c
const char* html = 
    "<div style=\"display: flex; flex-direction: row; gap: 10;\">"
        "<button onclick=\"count++\">+</button>"
        "<div>Count: {{count}}</div>"
        "<button onclick=\"count--\">-</button>"
    "</div>";

ReactUI* ui = ReactUI_Create(hInstance);
ReactUI_SetState(ui, "count", 0);
ReactUI_Render(ui, html);
ReactUI_CreateWindow(ui, "Counter", 400, 200);
```

### Centered Column Layout

```c
const char* html = 
    "<div style=\"display: flex; flex-direction: column; "
    "justify-content: center; align-items: center; gap: 15;\">"
        "<div style=\"font-size: 24;\">Score: {{score}}</div>"
        "<button onclick=\"score+=10\" style=\"width: 200;\">Add 10</button>"
        "<button onclick=\"score=0\" style=\"width: 200;\">Reset</button>"
    "</div>";
```

### Flex-Grow Distribution

```c
const char* html = 
    "<div style=\"display: flex; flex-direction: row; gap: 10;\">"
        "<div style=\"flex-grow: 1; background: #ff6b6b;\">Grow: 1</div>"
        "<div style=\"flex-grow: 2; background: #4ecdc4;\">Grow: 2</div>"
        "<div style=\"flex-grow: 1; background: #95e1d3;\">Grow: 1</div>"
    "</div>";
```

### Nested Flexbox (Dashboard)

```c
const char* html = 
    "<div style=\"display: flex; flex-direction: column; gap: 10;\">"
        "<!-- Header -->"
        "<div style=\"display: flex; flex-direction: row; "
        "justify-content: space-between; background: #1976D2;\">"
            "<div>Dashboard</div>"
            "<div>Score: {{score}}</div>"
        "</div>"
        
        "<!-- Main Content -->"
        "<div style=\"display: flex; flex-direction: row; flex-grow: 1; gap: 10;\">"
            "<div style=\"width: 200; background: #f0f0f0;\">Sidebar</div>"
            "<div style=\"flex-grow: 1; background: white;\">Main</div>"
        "</div>"
    "</div>";
```

## 🎨 Supported CSS Properties

### Layout
- `display` (flex)
- `flex-direction` (row, column, row-reverse, column-reverse)
- `justify-content` (flex-start, flex-end, center, space-between, space-around, space-evenly)
- `align-items` (flex-start, flex-end, center, stretch, baseline)
- `flex-wrap` (nowrap, wrap, wrap-reverse)
- `flex-grow` (number)
- `flex-shrink` (number)
- `flex-basis` (number or auto)
- `gap` (number in pixels)

### Sizing
- `width` (pixels)
- `height` (pixels)
- `padding` (pixels)

### Visual
- `background` (color name or #hex)
- `color` (text color)
- `font-size` (pixels)
- `border` (e.g., "2px solid #333")
- `border-radius` (pixels)

### Supported Colors
Named colors: red, blue, green, yellow, cyan, magenta, white, black, gray, grey, lightgray, darkgray, orange, purple, pink, brown

Hex colors: #RRGGBB format

## 🔒 Security Features

### Input Sanitization
```c
// onClick handlers are sanitized
// Only allowed: alphanumeric, +, -, =, space, underscore
onClick="count++"      // ✓ Valid
onClick="alert(1)"     // ✗ Blocked
onClick="count+=<script>" // ✗ Blocked
```

### Buffer Overflow Protection
All string operations use safe variants:
- `safe_strcpy()` - bounds-checked copy
- `safe_strcat()` - bounds-checked concatenation  
- `safe_sprintf()` - bounds-checked formatting

### State Validation
- Maximum 16 states per instance
- State names limited to 32 characters
- State operations validated

## 📊 Performance Characteristics

- **Parse Time**: O(n) where n is HTML length
- **Layout Calculation**: O(n) where n is node count
- **Render Time**: O(n) where n is visible nodes
- **Memory**: ~100 bytes per node + string storage

### Limitations

- Maximum 32 children per node
- Maximum 16 CSS properties per node
- Maximum 16 states per UI instance
- Maximum 8 concurrent UI instances
- Text content limited to 256 characters per node
- onClick handlers limited to 128 characters

## 🛠️ Compilation

### Using GCC (MinGW)
```bash
gcc example_flexbox.c -o example.exe -lgdi32 -luser32 -mwindows
```

### Using MSVC
```bash
cl example_flexbox.c /link user32.lib gdi32.lib
```

### Using CMake
```cmake
add_executable(example example_flexbox.c)
target_link_libraries(example gdi32 user32)
```

## 🐛 Debugging

Enable error callbacks to catch issues:

```c
void debugError(const char* msg) {
    FILE* f = fopen("errors.log", "a");
    fprintf(f, "[ERROR] %s\n", msg);
    fclose(f);
}

ReactUI_SetErrorCallback(ui, debugError);
```

## 🔄 Migration from Original Version

### Breaking Changes
1. `ReactUI_Create()` now requires explicit HINSTANCE
2. Global state removed - each instance is independent
3. String operations have stricter bounds checking

### Compatible Changes
- All public API functions unchanged
- HTML syntax fully backward compatible
- State management API identical

### Migration Example

**Old Code:**
```c
ReactUI* ui = ReactUI_Create(hInstance);
// global state was implicit
```

**New Code:**
```c
ReactUI* ui = ReactUI_Create(hInstance);
ReactUI_SetErrorCallback(ui, myErrorHandler); // Optional but recommended
```

## 🎓 Advanced Topics

### Dynamic State Updates

```c
// State changes trigger automatic re-render
ReactUI_SetState(ui, "score", 100);
// Window updates automatically

// Or force update
ReactUI_ForceUpdate(ui);
```

### Multiple Windows

```c
ReactUI* ui1 = ReactUI_Create(hInstance);
ReactUI* ui2 = ReactUI_Create(hInstance);
ReactUI* ui3 = ReactUI_Create(hInstance);

// Each has independent state
ReactUI_SetState(ui1, "counter", 0);
ReactUI_SetState(ui2, "counter", 100);
ReactUI_SetState(ui3, "counter", 200);

// Create separate windows
ReactUI_CreateWindow(ui1, "Window 1", 400, 300);
ReactUI_CreateWindow(ui2, "Window 2", 400, 300);
ReactUI_CreateWindow(ui3, "Window 3", 400, 300);
```

### Complex onClick Handlers

Supported operations:
- `count++` - Increment
- `count--` - Decrement
- `count+=10` - Add value
- `count-=5` - Subtract value
- `count=0` - Set value

```c
<button onclick="score+=100">Add 100</button>
<button onclick="lives-=1">Lose Life</button>
<button onclick="level++">Next Level</button>
<button onclick="health=100">Full Health</button>
```

## 📝 License

This is an improved version of the original React UI framework.
Free to use for personal and commercial projects.

## 🤝 Contributing

Improvements welcome! Areas for contribution:
- Additional CSS properties
- More layout modes (Grid)
- Animation support
- Event types beyond onClick
- Better text rendering

## 📞 Support

For bugs or feature requests, please provide:
1. Minimal reproduction code
2. Expected vs actual behavior
3. Compiler and Windows version
4. Error messages from callback

## 🎯 Roadmap

Future enhancements planned:
- [ ] CSS Grid layout
- [ ] Transitions and animations
- [ ] More event types (onHover, onFocus)
- [ ] External stylesheet support
- [ ] Component templates
- [ ] Virtual DOM diffing
- [ ] Accessibility features
- [ ] RTL text support

---

**Version**: 2.0.0  
**Last Updated**: 2026-02-06  
**Compatibility**: Windows 7+, MinGW/MSVC