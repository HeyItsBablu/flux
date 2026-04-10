# Installing and Running FluxUI

## Requirements

### Windows

| Requirement | Version |
|---|---|
| Windows | 10 or later |
| Visual Studio | 2022 (with **Desktop development with C++** workload) |
| CMake | 3.21 or later |
| Git | Any recent version |

> **Important:** FluxUI requires MSVC on Windows. GCC and Clang are not supported.

### Linux

| Requirement | Version |
|---|---|
| Ubuntu / Debian | 20.04 or later |
| GCC or Clang | GCC 11+ or Clang 13+ |
| CMake | 3.21 or later |
| Git | Any recent version |
| pkg-config | Any recent version |
| SDL2 | 2.0+ |
| Cairo + Pango | Any recent version |
| ALSA | Any recent version |
| FFmpeg | libavformat / libavcodec / libavutil / libswscale |
| OpenSSL | Any recent version (for HTTPS via curl) |

### Android

| Requirement | Version |
|---|---|
| Android Studio | Hedgehog (2023.1) or later |
| Android NDK | r27 or later |
| CMake | 3.22.1 (via SDK Manager) |
| Min SDK | API 24 (Android 7.0) |
| Target SDK | API 34+ |
| ABI | x86_64 (emulator) or arm64-v8a (device) |

---

## Option A — Use in your own project (recommended)

### Windows

CMake downloads and builds FluxUI automatically via FetchContent — no manual
cloning or dependency management required.

#### 1. Create your project folder
```
my_app/
    CMakeLists.txt
    main.cpp
```

#### 2. CMakeLists.txt
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

#### 3. main.cpp
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

#### 4. Open Developer PowerShell for VS 2022

Search for **Developer PowerShell for VS 2022** in the Start menu and open it.

> Do not use regular PowerShell or cmd — `cl.exe` is only available in the
> Developer PowerShell.

#### 5. Configure and build
```powershell
cd C:\path\to\my_app
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

#### 6. Run
```powershell
.\build\my_app.exe
```

---

### Linux

CMake downloads stb, Clipper2, and curl automatically via FetchContent.
SDL2, Cairo, Pango, ALSA, and FFmpeg are pulled from your system package
manager.

#### 1. Install system dependencies

**Ubuntu / Debian:**
```bash
sudo apt update
sudo apt install -y \
    build-essential git cmake ninja-build pkg-config \
    libsdl2-dev \
    libcairo2-dev libpango1.0-dev \
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
    alsa-lib-devel \
    ffmpeg-free-devel \
    openssl-devel
```

> FFmpeg packages are in the RPM Fusion repository on Fedora. If
> `ffmpeg-free-devel` is not found, enable RPM Fusion first:
> ```bash
> sudo dnf install https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm
> ```

#### 2. Create your project folder
```
my_app/
    CMakeLists.txt
    main.cpp
```

#### 3. CMakeLists.txt
```cmake
cmake_minimum_required(VERSION 3.21)
project(my_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(flux
    GIT_REPOSITORY https://github.com/Rosanchaudhary/flux.git
    GIT_TAG        v0.1.0
)
FetchContent_MakeAvailable(flux)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE flux::flux)
```

#### 4. main.cpp
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

int main() {
    FluxUI app;
    app.build([&]() { return createApp(&app); });
    app.createWindow("My App", 900, 700);
    return app.run();
}
```

#### 5. Configure and build
```bash
cd ~/path/to/my_app
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

The first configure will download stb, Clipper2, and curl — this may take a
minute. Subsequent configures use the cached downloads in `build/_deps/`.

#### 6. Run
```bash
./build/my_app
```

---

### Android

#### 1. Project structure
```
MyApplication/
└── app/
    └── src/
        └── main/
            ├── assets/                  ← images, certificates, and other bundled files go here
            │   ├── cacert.pem           ← SSL certificate bundle (required for HTTPS)
            │   └── images/
            │       └── logo.png
            ├── cpp/
            │   ├── flux/                ← FluxUI source tree (includes native entry point)
            │   │   ├── include/
            │   │   ├── src/
            │   │   ├── external/
            │   │   │   └── nanovg/
            │   │   └── CMakeLists.txt
            │   ├── app.hpp              ← createApp() declaration
            │   └── app.cpp              ← createApp() definition
            └── AndroidManifest.xml
```

#### 2. app-level CMakeLists.txt
```cmake
cmake_minimum_required(VERSION 3.22.1)
project(flux_android LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(FLUX_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/flux")

add_subdirectory(${FLUX_SOURCE_DIR} flux_build)

add_library(native-lib SHARED
        app.cpp
)

target_link_libraries(native-lib PRIVATE
        flux::flux
        aaudio
        mediandk
        camera2ndk
)

target_link_options(native-lib PRIVATE
        "-Wl,-u,ANativeActivity_onCreate"
        "-Wl,-u,android_main"
)
```

> **Note:** `native-lib.cpp` is now part of the FluxUI source tree — you no
> longer need to include it in your project. The entry point (`android_main`)
> is provided by flux internally and exposed via the `-Wl,-u,android_main`
> linker option above.

#### 3. app.hpp
```cpp
#pragma once
#include "flux/flux.hpp"

WidgetPtr createApp(FluxUI* app);  // declaration only — no body here
```

#### 4. app.cpp
```cpp
#include "app.hpp"
#include "flux/flux.hpp"

class MyApp : public Component {
public:
    WidgetPtr build() override {
        return Scaffold(
            AppBar("My App"),
            Center(
                Text("Hello from FluxUI!")
                    ->setFontSize(24)
                    ->setFontWeight(FontWeight::Bold)
            )
        );
    }
};

WidgetPtr createApp(FluxUI* app) {
    return FluxApp("My App", BuildComponent<MyApp>(), AppTheme::light(),
                   false,  // debugShowWidgetBounds
                   900,    // width  (logical, not pixels)
                   700,    // height
                   false,  // maximize
                   false   // fullscreen
    );
}
```

#### 5. AndroidManifest.xml

Replace the contents of `app/manifests/AndroidManifest.xml` with:
```xml
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android">

    <!-- ── Network ─────────────────────────────────────────────────────── -->
    <uses-permission android:name="android.permission.INTERNET" />
    <uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />

    <!-- ── Audio ───────────────────────────────────────────────────────── -->
    <uses-permission android:name="android.permission.RECORD_AUDIO" />

    <!-- ── Camera ──────────────────────────────────────────────────────── -->
    <uses-permission android:name="android.permission.CAMERA" />
    <uses-feature android:name="android.hardware.camera" android:required="false" />
    <uses-feature android:name="android.hardware.camera.front" android:required="false" />
    <uses-feature android:name="android.hardware.camera.autofocus"/>

    <!-- ── Storage (for saving photos/recordings) ──────────────────────── -->
    <uses-permission android:name="android.permission.READ_MEDIA_IMAGES" />
    <uses-permission android:name="android.permission.READ_MEDIA_AUDIO" />
    <uses-permission android:name="android.permission.READ_MEDIA_VIDEO" />
    <uses-permission android:name="android.permission.READ_EXTERNAL_STORAGE"
        android:maxSdkVersion="32"/>

    <application
        android:allowBackup="false"
        android:icon="@mipmap/ic_launcher"
        android:label="@string/app_name"
        android:roundIcon="@mipmap/ic_launcher_round"
        android:supportsRtl="true">

        <activity
            android:name="android.app.NativeActivity"
            android:label="FluxUI"
            android:exported="true"
            android:screenOrientation="portrait"
            android:configChanges="orientation|keyboardHidden|screenSize|density"
            android:theme="@android:style/Theme.NoTitleBar.Fullscreen">

            <meta-data
                android:name="android.app.lib_name"
                android:value="native-lib" />

            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>

    </application>

</manifest>
```

Key points:

- `android.app.NativeActivity` is the built-in Android activity class that
  hosts a native C++ app — no Java/Kotlin code needed.
- `android.app.lib_name` must exactly match the name passed to
  `add_library(native-lib ...)` in your CMakeLists.txt.
- `Theme.NoTitleBar.Fullscreen` gives FluxUI the full screen with no
  system chrome, matching the Win32 behaviour.
- `configChanges` prevents Android from destroying and recreating the
  activity on rotation or density changes — FluxUI handles resize itself
  via `APP_CMD_CONFIG_CHANGED`.
- `screenOrientation="portrait"` locks orientation. Change to `landscape`
  or remove the attribute entirely to allow free rotation.
- `android.hardware.camera.autofocus` declares autofocus capability as
  a feature. It is not marked `required="false"` so devices without
  autofocus will see this app as incompatible on the Play Store — remove
  the line if you want to support fixed-focus devices.

#### 6. SSL certificate bundle

FluxUI uses `cacert.pem` for HTTPS certificate verification. Place the
certificate bundle at:

```
app/src/main/assets/cacert.pem
```

The file is read from the asset manager at runtime using the path
`cacert.pem` (no leading slash). You can download the latest bundle from
[curl's CA certificate page](https://curl.se/docs/caextract.html) or copy
it from your development machine:

- **Windows:** `C:\Users\<you>\AndroidStudioProjects\MyApplication3\app\src\main\assets\cacert.pem`
- **Linux:** typically at `/etc/ssl/certs/ca-certificates.crt`

> **Important:** Reference this file in code **without** a leading slash,
> just like any other asset:
> ```cpp
> // Correct
> FluxHTTP::setCABundle("cacert.pem");
>
> // Wrong — leading slash bypasses AAssetManager and will fail
> FluxHTTP::setCABundle("/cacert.pem");
> ```

#### 7. Bundling images and assets

All files placed under `src/main/assets/` are packaged into the APK
automatically by Gradle. Reference them in code **without** a leading slash:
```
src/main/assets/images/logo.png  →  Image("images/logo.png")
src/main/assets/data/config.json →  "data/config.json"
src/main/assets/cacert.pem       →  "cacert.pem"
```

For files outside the APK (downloads, camera photos, external storage),
use an absolute path with a leading slash — these go through the filesystem
directly and bypass `AAssetManager`:
```cpp
Image("/sdcard/Download/photo.jpg")
```

#### 8. Build and run

In Android Studio:

1. Open the project
2. Select your emulator or connected device in the toolbar
3. Click **Run ▶**

From the command line:
```bash
./gradlew assembleDebug
adb install app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.yourpackage/.MainActivity
```

---

## Option B — Clone and build FluxUI directly

Use this if you want to browse the source or run the built-in examples.

Each example is a single `.cpp` file that only implements `createApp()`. The
build system supplies the platform entry point automatically
(`main_win32.cpp` on Windows, `main_linux.cpp` on Linux) and links
`flux::flux`, so no boilerplate is needed in the example itself.

### Windows

#### 1. Clone the repo
```powershell
git clone https://github.com/Rosanchaudhary/flux.git
cd flux
```

#### 2. Open Developer PowerShell for VS 2022

Search for **Developer PowerShell for VS 2022** in the Start menu and open it,
then navigate back:
```powershell
cd C:\path\to\flux
```

#### 3. Configure
```powershell
cmake -S . -B build/msvc -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFLUX_BUILD_EXAMPLES=ON
```

#### 4. Build
```powershell
cmake --build build/msvc
```

#### 5. Run an example
```powershell
.\build\msvc\examples\futuretest.exe
```

---

### Linux

#### 1. Install system dependencies

See [Option A → Linux → Step 1](#1-install-system-dependencies) — the same
packages are required.

#### 2. Clone the repo
```bash
git clone https://github.com/Rosanchaudhary/flux.git
cd flux
```

#### 3. Configure
```bash
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFLUX_BUILD_EXAMPLES=ON
```

#### 4. Build
```bash
cmake --build build/linux
```

#### 5. Run an example
```bash
./build/linux/examples/futuretest
```

---

## Build options

| CMake flag | Default | Description |
|---|---|---|
| `FLUX_BUILD_EXAMPLES` | `OFF` | Build the example applications |
| `FLUX_BUILD_TESTS` | `OFF` | Build the test suite |

---

## Build configurations

| Configuration | Flag | Description |
|---|---|---|
| Debug | `-DCMAKE_BUILD_TYPE=Debug` | Includes `FLUX_DEBUG` define, no optimizations |
| Release | `-DCMAKE_BUILD_TYPE=Release` | Full optimizations, smaller binary |

---

## Using CMakePresets.json (Windows only)
```powershell
cmake --preset msvc-debug
cmake --build build/msvc
```

Available presets: `msvc-debug`, `msvc-release`