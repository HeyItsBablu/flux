# Installing and Running FluxUI

## How it works

Your entire app lives in one file — `lib/main.cpp`. Assets go in `assets/`.
Everything else (platform entry points, cmake, asset copying) is handled by
FluxUI internally. To build and run, execute the script for your platform.

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

---

## Your app

### lib/main.cpp

```cpp
#include "flux/flux.hpp"

class MyApp : public Widget {
    State<int> counter{0};
public:
    WidgetPtr build() override {
        return Flex({
            Text(counter)->setFontSize(18),
            Button("Click", [this] { counter++; })
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

### Assets

Place any images, fonts, or data files in the `assets/` folder. They are
copied to the output directory automatically on every build.

```
assets/
├── images/
│   └── logo.png        →  Image("images/logo.png")
├── videos/
│   └── video.mp4       →  VideoPlayer("videos/video.mp4")
└── audio.mp3           →  AudioPlayer("audio.mp4")
```

---

## Build configurations

| Configuration | Description |
|---|---|
| Debug | Enables `FLUX_DEBUG`, assertions, no optimizations |
| Release | Full optimizations, smaller binary |

## CMakePresets (advanced)

If you need to invoke cmake directly instead of using the scripts:

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