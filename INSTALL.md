# Installing and Running FluxUI

## Requirements

| Requirement | Version |
|---|---|
| Windows | 10 or later |
| Visual Studio | 2022 (with **Desktop development with C++** workload) |
| CMake | 3.21 or later |
| Git | Any recent version |

> **Important:** FluxUI requires MSVC. GCC and Clang are not supported.

---

## Option A — Use in your own project (recommended)

This is the fastest way to get started. CMake downloads and builds FluxUI automatically via FetchContent — no manual cloning or dependency management required.

### 1. Create your project folder

```
my_app/
    CMakeLists.txt
    main.cpp
```

### 2. CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.21)
project(my_app LANGUAGES CXX)

if(NOT MSVC)
    message(FATAL_ERROR "Requires MSVC.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(flux
    GIT_REPOSITORY https://github.com/Rosanchaudhary/flux.git
    GIT_TAG        v0.1.0
)
FetchContent_MakeAvailable(flux)

add_executable(my_app WIN32 main.cpp)
target_link_libraries(my_app PRIVATE flux::flux)
target_link_options(my_app PRIVATE /SUBSYSTEM:WINDOWS)
```

### 3. main.cpp

```cpp
#include "flux/flux.hpp"

class MyApp : public Component {
public:
    WidgetPtr build() override {
        return Scaffold(
            AppBar("My App"),
            Center(
                Text("Hello from FluxUI!")
                    ->setFontSize(32)
                    ->setFontWeight(FontWeight::Bold)
            )
        );
    }
};

WidgetPtr createApp(FluxUI* app) {
    return FluxApp("My App", BuildComponent<MyApp>(), AppTheme::light());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    FluxUI app(hInstance);
    app.build([&]() { return createApp(&app); });
    app.createWindow("My App", 900, 700);
    return app.run();
}
```

### 4. Open Developer PowerShell for VS 2022

Search for **Developer PowerShell for VS 2022** in the Start menu and open it.

> Do not use regular PowerShell or cmd — `cl.exe` is only available in the Developer PowerShell.

### 5. Configure and build

```powershell
cd C:\path\to\my_app
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

### 6. Run

```powershell
.\build\my_app.exe
```

---

## Option B — Clone and build FluxUI directly

Use this if you want to browse the source or run the built-in examples.

### 1. Clone the repo

```powershell
git clone https://github.com/Rosanchaudhary/flux.git
cd flux
```

### 2. Open Developer PowerShell for VS 2022

Search for **Developer PowerShell for VS 2022** in the Start menu and open it, then navigate back:

```powershell
cd C:\path\to\flux
```

### 3. Configure

```powershell
cmake -S . -B build/msvc -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFLUX_BUILD_EXAMPLES=ON
```

### 4. Build

```powershell
cmake --build build/msvc
```

### 5. Run an example

```powershell
.\build\msvc\examples\main.exe
```

Other available examples:

```powershell
.\build\msvc\examples\counter.exe
.\build\msvc\examples\component.exe
.\build\msvc\examples\draggable.exe
.\build\msvc\examples\layout.exe
.\build\msvc\examples\listview.exe
.\build\msvc\examples\graphtest.exe
.\build\msvc\examples\illustrator.exe
.\build\msvc\examples\image_editor.exe
.\build\msvc\examples\overlaytest.exe
.\build\msvc\examples\paintfull.exe
.\build\msvc\examples\logic_sim.exe
```

> **Note:** `image_editor` uses stb which is fetched automatically when building examples. No extra steps needed.

---

## Build options

| CMake flag | Default | Description |
|---|---|---|
| `FLUX_BUILD_EXAMPLES` | `OFF` | Build the example applications |
| `FLUX_BUILD_TESTS` | `OFF` | Build the test suite |

Pass any flag at configure time:

```powershell
cmake -S . -B build/msvc -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFLUX_BUILD_EXAMPLES=ON -DFLUX_BUILD_TESTS=ON
```

---

## Build configurations

| Configuration | Flag | Description |
|---|---|---|
| Debug | `-DCMAKE_BUILD_TYPE=Debug` | Includes `FLUX_DEBUG` define, no optimizations |
| Release | `-DCMAKE_BUILD_TYPE=Release` | Full optimizations, smaller binary |

---

## Using CMakePresets.json

If you cloned the repo and want to use presets instead of typing flags manually:

```powershell
cmake --preset msvc-debug
cmake --build build/msvc
```

Available presets: `msvc-debug`, `msvc-release`

---

## Troubleshooting

**`cl.exe` not found / `CMAKE_CXX_COMPILER` could not be found`**

You are not in Developer PowerShell. Close your current terminal, search for
**Developer PowerShell for VS 2022** in the Start menu, and run the cmake
commands from there. Regular PowerShell and cmd do not have `cl.exe` on PATH.

---

**`ninja: error: loading 'build.ninja': The system cannot find the file specified`**

You ran `cmake --build` before running the configure step. Always configure first.
If the error persists after configuring, the build folder may be stale — delete it and start fresh:

```powershell
Remove-Item -Recurse -Force build/msvc
cmake -S . -B build/msvc -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFLUX_BUILD_EXAMPLES=ON
cmake --build build/msvc
```

---

**`ninja: error: build.ninja:35: loading 'CMakeFiles\rules.ninja'`**

Your build folder has a stale cache from a previous CMake configuration. Delete it and reconfigure:

```powershell
Remove-Item -Recurse -Force build/msvc
cmake -S . -B build/msvc -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/msvc
```

---

**`Generator Ninja does not support toolset specification`**

Remove the `"toolset"` line from your `CMakePresets.json`. The `toolset` field is only
valid for Visual Studio generators (`"Visual Studio 17 2022"`), not Ninja.

---

**`Cannot find source file: external/Clipper2/...`**

Your `CMakeLists.txt` still has the old manual `add_library(clipper2 ...)` block.
Replace it with the FetchContent version used in the current root `CMakeLists.txt`.
Clipper2 and glad are both fetched automatically at configure time — the
`external/` folder is no longer needed and can be deleted.

---

**First configure takes a long time**

This is normal. CMake is downloading Clipper2 via FetchContent on first run.
Subsequent configures use the cached download in `build/msvc/_deps/`.