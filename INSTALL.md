# Installing and Running FluxUI

## How it works

Your entire app lives in one file — `lib/main.cpp`. Assets go in `assets/`.
Window title/size/fullscreen/maximize state lives in `config/AppConfig.json`
(see [AppConfig.json](#appconfigjson) below) — not in code. Everything else
(platform entry points, CMake, asset copying) is handled by FluxUI
internally.

You can build and run either with the per-platform scripts below, or with
the `flux` CLI (see [The flux CLI](#the-flux-cli)) — they're separate,
parallel tools that both drive the same underlying CMake build; use
whichever fits your workflow.

---

## Windows

### Requirements

| Requirement | Version |
|---|---|
| Windows | 10 or later |
| Visual Studio | 2022 (with **Desktop development with C++** workload) |
| CMake | 3.21 or later |
| Git | Any recent version |

### Run

Open **Developer PowerShell for VS 2022** (search for it in the Start menu)
and run:

```bat
scripts\run-windows.bat
```

> Do not use regular PowerShell or cmd — `cl.exe` is only available in the
> Developer PowerShell.

---

## Linux

### Requirements

| Requirement | Version |
|---|---|
| Ubuntu / Debian | 20.04 or later |
| GCC or Clang | GCC 11+ or Clang 13+ |
| CMake | 3.21 or later |
| Git | Any recent version |

### Install dependencies

**Ubuntu / Debian:**
```bash
sudo apt update
sudo apt install -y \
    build-essential git cmake ninja-build pkg-config \
    libsdl2-dev \
    libcairo2-dev libpango1.0-dev \
    libjpeg-dev \
    libasound2-dev \
    libavformat-dev libavcodec-dev libavutil-dev libswscale-dev \
    libssl-dev
```

**Fedora / RHEL:**
```bash
sudo dnf install -y \
    gcc-c++ git cmake ninja-build pkg-config \
    SDL2-devel \
    cairo-devel pango-devel \
    libjpeg-turbo-devel \
    alsa-lib-devel \
    ffmpeg-free-devel \
    openssl-devel
```

> FFmpeg packages are in the RPM Fusion repository on Fedora. If
> `ffmpeg-free-devel` is not found, enable RPM Fusion first:
> ```bash
> sudo dnf install https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm
> ```

### Run

```bash
chmod +x scripts/run-linux.sh
scripts/run-linux.sh
```

---

## macOS

### Requirements

| Requirement | Version |
|---|---|
| macOS | 12 (Monterey) or later |
| Xcode | 14 or later (with Command Line Tools) |
| CMake | 3.21 or later |
| Git | Any recent version |

### Install dependencies

```bash
xcode-select --install
brew install cmake ninja
```

### Run

```bash
chmod +x scripts/run-macos.sh
scripts/run-macos.sh
```

---

## Android

### Requirements

| Requirement | Version |
|---|---|
| Android Studio | Hedgehog (2023.1) or later |
| Android NDK | r27 or later |
| CMake | 3.22.1 (via SDK Manager) |
| Min SDK | API 24 (Android 7.0) |
| Target SDK | API 34+ |
| ABI | x86_64 (emulator) or arm64-v8a (device) |

### Run

```bat
scripts\run-android.bat
```

> The `flux` CLI does not support Android yet (`flux run android` /
> `flux doctor android` will report "not implemented"). Use the script
> above for this platform.

---

## Web

### Requirements

| Requirement | Version |
|---|---|
| Emscripten | 3.1.0 or later |
| CMake | 3.21 or later |
| Python | 3 (required by Emscripten) |
| Git | Any recent version |

### Run

```bat
scripts\run-web.bat
```

Web builds default to the DOM renderer (`FLUX_WEB_RENDERER=dom`), which
supports server-side rendering + hydration (see [SSR](#ssr-server-side-rendering)
below). Pass `-DFLUX_WEB_RENDERER=canvas` at configure time to use the
older Canvas2D renderer instead.

> Same as Android — the `flux` CLI does not support Web yet. Use the
> script above.

---

## SSR (server-side rendering)

FluxUI also ships a native, headless SSR host (`flux_ssr`) that renders
your app's DOM output on the server and hands the browser a hydration
blob to boot from — no browser or GPU required to render a page.

SSR is off by default and built as its own target, separate from the
desktop `flux` library:

```bash
cmake -B build/ssr -DFLUX_BUILD_SSR=ON
cmake --build build/ssr
```

On Linux, if you only want the SSR binary and not a full desktop build
(skipping the SDL2/Cairo/Pango/ALSA/FFmpeg dependencies from the Linux
section above), add `-DFLUX_BUILD_DESKTOP=OFF`:

```bash
cmake -B build/ssr -DFLUX_BUILD_SSR=ON -DFLUX_BUILD_DESKTOP=OFF
```

Run the resulting binary with a port (and optional worker-thread count):

```bash
./build/ssr/ssr/flux_ssr 8080          # defaults to hardware_concurrency() workers
./build/ssr/ssr/flux_ssr 8080 16       # 16 worker threads
```

`flux_ssr` requires the same web bundle a browser would load
(`flux_app.js/.wasm/.data`) to be built first, since it serves that
bundle for the hydration handoff — build the Web target before the SSR
target, or your first request will log a warning that hydration can
never boot.

---

## Your app

### lib/main.cpp

```cpp
#include "flux/flux.hpp"

class MyApp : public Widget
{
    State<int> counter = 0;

public:
    WidgetPtr build() override
    {
        constexpr int kFabSize = 56;
        constexpr int kAppBarHeight = 56;

        auto appBar =
            Box({
                    Text("My App")
                        ->setFontSize(20)
                        ->setFontWeight(FontWeight::Bold)
                        ->setTextColor(Color::fromRGB(255, 255, 255)),
                })
                ->setDisplay(Display::Flex)
                ->setDirection(FlexDirection::Row)
                ->setAlignItems(AlignItems::Center)
                ->setJustifyContent(JustifyContent::Start)
                ->setPaddingHV(16, 0)
                ->setWidthMode(SizeMode::Full)
                ->setHeightMode(SizeMode::Fixed)
                ->setHeight(kAppBarHeight)
                ->setBackgroundColor(Color::fromRGB(33, 150, 243));

        auto fab =
            Box({
                    Text("+")
                        ->setFontSize(28)
                        ->setFontWeight(FontWeight::Bold)
                        ->setTextColor(Color::fromRGB(255, 255, 255)),
                })
                ->setDisplay(Display::Flex)
                ->setAlignItems(AlignItems::Center)
                ->setJustifyContent(JustifyContent::Center)
                ->setWidthMode(SizeMode::Fixed)
                ->setWidth(kFabSize)
                ->setHeightMode(SizeMode::Fixed)
                ->setHeight(kFabSize)
                ->setBorderRadius(kFabSize / 2)
                ->setBackgroundColor(Color::fromRGB(33, 150, 243))
                ->setPositionMode(Position::Absolute)
                ->setBottomPx(24)
                ->setRightPx(24)
                ->setZIndexVal(1)
                ->setOnClick([this]
                             { counter++; });

        auto body =
            Box({
                    Text("You have pushed the button this many times:")
                        ->setFontSize(14)
                        ->setTextColor(Color::fromRGB(90, 90, 90)),
                    Text(counter)
                        ->setFontSize(40)
                        ->setFontWeight(FontWeight::Bold),
                })
                ->setDisplay(Display::Flex)
                ->setDirection(FlexDirection::Column)
                ->setAlignItems(AlignItems::Center)
                ->setJustifyContent(JustifyContent::Center)
                ->setGap(8)
                ->setWidthMode(SizeMode::Full)
                ->setHeightMode(SizeMode::Full)
                ->setFlexGrow(1); // takes all remaining height below the app bar

        return Box({
                       appBar,
                       body,
                       fab,
                   })
            ->setDisplay(Display::Flex)
            ->setDirection(FlexDirection::Column)
            ->setWidthMode(SizeMode::Full)
            ->setHeightMode(SizeMode::Full)
            ->setBackgroundColor(Color::fromRGB(245, 245, 250));
    }
};

// ============================================================
//  Entry point
// ============================================================

WidgetPtr createApp(FluxUI *app)
{
    return FluxApp()
        .setTheme(AppTheme::light())
        .build(std::make_shared<MyApp>());
}
```

`FluxApp()` only configures **theme** and debug options
(`.setTheme(...)`, `.setDebugWidgetBounds(true)`) before `.build(home)`.
It does **not** take title/width/height — those are read directly from
`config/AppConfig.json` by each platform's entry point before your
widget tree is ever built. See below.

### AppConfig.json

Native window setup, app identity, and versioning are configured once in
`config/AppConfig.json` at the project root, not in `lib/main.cpp`.
`config/AppConfig.cmake` reads this file at configure time (and
re-configures automatically whenever you edit it) and generates
`AppConfig.generated.h`, which each platform's entry point
(`windows/main.cpp`, `macos/main.mm`, `linux/main.cpp`,
`flux_android_main.cpp`) includes directly.

```json
{
  "name":     "My Application",
  "bundleId": "com.example.myapplication",
  "version":  "1.0.0",
  "build":    1,
  "window": {
    "width":      1280,
    "height":     720,
    "fullscreen": false,
    "maximize":   true
  }
}
```

This generates:

- `FLUX_APP_NAME` — window title
- `FLUX_APP_BUNDLE_ID` — app bundle/package identifier
- `FLUX_APP_VERSION` — also fed directly into the CMake project version
  (`project(flux VERSION ${FLUX_APP_VERSION} ...)`), so it must be a
  plain dotted version string like `"1.0.0"`
- `FLUX_APP_BUILD` — a plain integer build number, unquoted
- `FLUX_APP_WINDOW_WIDTH` / `FLUX_APP_WINDOW_HEIGHT` — initial window size
- `FLUX_APP_FULLSCREEN` / `FLUX_APP_MAXIMIZE` — startup window state
  (booleans in JSON, generated as `1`/`0` in the header)

Web builds ignore the entire `window` block — the canvas always fills the
browser viewport regardless of what's configured here.

### Assets

Place any images, fonts, or data files in the `assets/` folder. They are
copied to the output directory automatically on every build.

```
assets/
├── images/
│   └── logo.png        →  Image("images/logo.png")
├── videos/
│   └── video.mp4       →  VideoPlayer("videos/video.mp4")
└── audio.mp3            →  AudioPlayer("audio.mp3")
```

---

## The flux CLI

In addition to the per-platform scripts above, native desktop builds
(Windows, macOS, Linux) also produce a `flux` CLI binary
(`cli/` subdirectory, built automatically as part of the normal CMake
build on those three hosts). Once you have it built once via a script or
preset, you can use it for subsequent builds:

```bash
flux run <platform> [--release]     # Build and launch
flux build <platform> [--release]   # Build only
flux doctor [platform]              # Check host toolchain
flux add <package> [--ref <ref>]    # Add a dependency
flux remove <package>               # Remove a dependency
flux install                        # Install dependencies from flux.deps.json
```

Supported platforms: `windows`, `linux`, `macos`. `web` and `android` are
recognized as valid platform names but are **not implemented yet** — the
CLI will tell you so rather than erroring as "unknown platform." Use the
platform-specific scripts above for those two.

Each native subcommand must be run from a matching host — e.g.
`flux run windows` only works when the CLI itself was built and is
running on Windows; running it from macOS/Linux reports that a Windows
host is required, rather than silently failing.

> **`--release` is currently a no-op.** The CLI accepts the flag but
> always builds Debug and prints a note saying so — there is no way to
> produce a Release build via the CLI today. Use the CMake presets below
> directly if you need a Release binary.

---

## Build configurations

| Configuration | Description |
|---|---|
| Debug | Enables `FLUX_DEBUG`, assertions, no optimizations |
| Release | Full optimizations, smaller binary — **build directly via CMake presets**, not yet available through the `flux` CLI (see above) |

## CMakePresets (advanced)

If you need to invoke cmake directly instead of using the scripts or CLI:

| Preset | Platform | Compiler | Config |
|---|---|---|---|
| `msvc-debug` | Windows | MSVC | Debug |
| `msvc-release` | Windows | MSVC | Release |
| `gcc-debug` | Linux | GCC | Debug |
| `gcc-release` | Linux | GCC | Release |
| `clang-debug` | Linux | Clang | Debug |

```bash
cmake --preset gcc-debug
cmake --build build/linux
```

> This table only lists the presets I've confirmed. There may be
> additional presets for macOS/Android/Web — run `cmake --list-presets`
> from the project root to see the full, current list.