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
| SDL2 | 2.0+ |
| Cairo + Pango | Any recent version |

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

This is the fastest way to get started. CMake downloads and builds FluxUI
automatically via FetchContent — no manual cloning or dependency management
required.

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

### Android

#### 1. Project structure
```
MyApplication/
└── app/
    └── src/
        └── main/
            ├── assets/                  ← images and other bundled files go here
            │   └── images/
            │       └── logo.png
            ├── cpp/
            │   ├── flux/                ← FluxUI source tree
            │   │   ├── include/
            │   │   ├── src/
            │   │   ├── external/
            │   │   │   └── nanovg/
            │   │   └── CMakeLists.txt
            │   ├── app.hpp              ← createApp() declaration
            │   ├── app.cpp              ← createApp() definition
            │   └── native-lib.cpp       ← android_main entry point
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

add_library(nanovg STATIC
        ${FLUX_SOURCE_DIR}/external/nanovg/src/nanovg.c
        ${FLUX_SOURCE_DIR}/external/nanovg/src/nanovg_gl_impl.c
)
set_source_files_properties(
        ${FLUX_SOURCE_DIR}/external/nanovg/src/nanovg.c
        PROPERTIES LANGUAGE C
)
target_include_directories(nanovg PUBLIC
        ${FLUX_SOURCE_DIR}/external/nanovg/src
)

add_subdirectory(${FLUX_SOURCE_DIR} flux_build)

add_library(native-lib SHARED
        native-lib.cpp
        app.cpp
#        flux_android_jni.cpp
)

target_include_directories(native-lib PRIVATE
        ${FLUX_SOURCE_DIR}/include
        ${FLUX_SOURCE_DIR}/external/nanovg/src
        ${ANDROID_NDK}/sources/android/native_app_glue
)

target_compile_definitions(native-lib PRIVATE
        NANOVG_GLES2
)

target_link_libraries(native-lib PRIVATE
        nanovg
        flux::flux
        aaudio
        log
        android
        EGL
        GLESv2
        mediandk
)

# mediandk requires API 21+
#target_compile_definitions(fluxui PRIVATE __ANDROID_MIN_SDK_VERSION__=21)

target_link_options(native-lib PRIVATE
        "-Wl,-u,ANativeActivity_onCreate"
)
```

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

#### 5. native-lib.cpp
```cpp
// native-lib.cpp
#include <android_native_app_glue.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <jni.h>

#include "flux/flux.hpp"
#include "app.hpp"
#include "nanovg.h"
#include "nanovg_gl.h"

// ── Statics ───────────────────────────────────────────────────────────────────
static FluxUI*        s_app          = nullptr;
static NVGcontext*    s_vg           = nullptr;
static AAssetManager* s_assetManager = nullptr;

// ── NEW: JVM + EGL handles needed by FluxVideo ────────────────────────────────
static JavaVM*    s_jvm        = nullptr;
static EGLDisplay s_eglDisplay = EGL_NO_DISPLAY;
static EGLContext s_eglContext = EGL_NO_CONTEXT;
static EGLSurface s_eglSurface = EGL_NO_SURFACE;   // the window surface

// ── Global accessors ──────────────────────────────────────────────────────────
extern void FluxAndroid_setVG(NVGcontext* vg);

void           FluxAndroid_setAssetManager(AAssetManager* am) { s_assetManager = am; }
AAssetManager* FluxAndroid_getAssetManager()                  { return s_assetManager; }

// EGL accessors used by FluxVideo decode thread
EGLDisplay FluxAndroid_getEGLDisplay() { return s_eglDisplay; }
EGLContext FluxAndroid_getEGLContext() { return s_eglContext;  }
EGLSurface FluxAndroid_getEGLSurface() { return s_eglSurface; }

extern void  FluxAndroid_setDpiScale(float scale);
extern float FluxAndroid_getDpiScale();

// ── JNI helper: attach current thread and return a JNIEnv* ───────────────────
static JNIEnv* getJNIEnv() {
    if (!s_jvm) return nullptr;
    JNIEnv* env = nullptr;
    jint ret = s_jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (ret == JNI_EDETACHED) {
        s_jvm->AttachCurrentThread(&env, nullptr);
    }
    return env;
}

// ============================================================================
// FluxVideo JNI shims
// Declared in flux_video.hpp — implemented here where we have s_jvm.
// ============================================================================

void* FluxVideo_createSurfaceTexture(GLuint texId) {
    JNIEnv* env = getJNIEnv();
    if (!env) return nullptr;

    jclass    cls  = env->FindClass("android/graphics/SurfaceTexture");
    jmethodID ctor = env->GetMethodID(cls, "<init>", "(I)V");
    jobject   st   = env->NewObject(cls, ctor, (jint)texId);
    if (!st) return nullptr;

    // Return a global ref — survives across JNI calls and threads
    return reinterpret_cast<void*>(env->NewGlobalRef(st));
}

void FluxVideo_updateTexImage(void* surfaceTexture) {
    JNIEnv* env = getJNIEnv();
    if (!env || !surfaceTexture) return;

    jobject   st  = reinterpret_cast<jobject>(surfaceTexture);
    jclass    cls = env->GetObjectClass(st);
    jmethodID mid = env->GetMethodID(cls, "updateTexImage", "()V");
    env->CallVoidMethod(st, mid);
}

ANativeWindow* FluxVideo_getNativeWindow(void* surfaceTexture) {
    JNIEnv* env = getJNIEnv();
    if (!env || !surfaceTexture) return nullptr;

    jobject   st         = reinterpret_cast<jobject>(surfaceTexture);
    jclass    surfCls    = env->FindClass("android/view/Surface");
    jmethodID surfCtor   = env->GetMethodID(surfCls, "<init>",
                                            "(Landroid/graphics/SurfaceTexture;)V");
    jobject   surface    = env->NewObject(surfCls, surfCtor, st);
    if (!surface) return nullptr;

    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    env->DeleteLocalRef(surface);
    return window;
}

void FluxVideo_destroySurfaceTexture(void* surfaceTexture) {
    JNIEnv* env = getJNIEnv();
    if (!env || !surfaceTexture) return;
    env->DeleteGlobalRef(reinterpret_cast<jobject>(surfaceTexture));
}

// ── NVG_initOESBlit forward (defined in nanovg_oes_ext.cpp) ──────────────────
extern void NVG_initOESBlit(int maxW, int maxH);

// ── Input ─────────────────────────────────────────────────────────────────────
static int32_t handle_input(android_app* app, AInputEvent* event) {
    if (s_app)
        s_app->getPlatformWindow().handleAndroidEvent(event);
    return 1;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────
static void handle_cmd(android_app* app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW: {
            if (!app->window) break;

            // ── Store JVM (needed by FluxVideo JNI shims) ─────────────────────
            s_jvm = app->activity->vm;

            FluxJNI::init(app);
            FluxAndroid_setAssetManager(app->activity->assetManager);

            AConfiguration* config = AConfiguration_new();
            AConfiguration_fromAssetManager(config, app->activity->assetManager);
            float dpiScale = AConfiguration_getDensity(config) / 160.0f;
            AConfiguration_delete(config);
            FluxAndroid_setDpiScale(dpiScale);

            s_app = new FluxUI(app);
            s_app->build([&]() { return createApp(s_app); });

            auto* cfg = static_cast<FluxAppWidget*>(FluxAppWidget::getInstance());
            s_app->createWindow(cfg->title, cfg->windowWidth, cfg->windowHeight);

            s_vg = nvgCreateGLES2(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
            nvgCreateFont(s_vg, "default", "/system/fonts/Roboto-Regular.ttf");
            FluxAndroid_setVG(s_vg);

            // ── Grab EGL handles AFTER FluxUI creates the window/surface ──────
            auto& win      = s_app->getPlatformWindow();
            s_eglDisplay   = win.getEGLDisplay();
            s_eglContext   = win.getEGLContext();
            s_eglSurface   = win.getEGLSurface();

            // ── Init OES→Tex2D blit helper (max resolution you expect) ────────
            // 1920×1080 covers 1080p; bump to 3840×2160 for 4K assets
            NVG_initOESBlit(1920, 1080);

            s_app->getFontCache().clear();
            s_app->rebuild();
            break;
        }

        case APP_CMD_TERM_WINDOW:
            // Close any open video before destroying GL context
            FluxVideo::get().close();

            if (s_vg)  { nvgDeleteGLES2(s_vg); s_vg = nullptr; }
            if (s_app) { delete s_app; s_app = nullptr; }

            s_eglDisplay = EGL_NO_DISPLAY;
            s_eglContext = EGL_NO_CONTEXT;
            s_eglSurface = EGL_NO_SURFACE;
            break;

        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONFIG_CHANGED:
            if (s_app) {
                auto& win = s_app->getPlatformWindow();
                win.updateClientSize();
                auto gc = win.getMeasureContext();
                if (win.callbacks.onResize)
                    win.callbacks.onResize(gc, win.clientWidth(), win.clientHeight());
            }
            break;

        case APP_CMD_LOST_FOCUS:
            if (s_app && s_app->getPlatformWindow().callbacks.onMouseLeave)
                s_app->getPlatformWindow().callbacks.onMouseLeave();
            break;

        default: break;
    }
}

// ── Main loop ─────────────────────────────────────────────────────────────────
void android_main(android_app* app) {
    app->onAppCmd     = handle_cmd;
    app->onInputEvent = handle_input;

    while (true) {
        int events;
        android_poll_source* source;
        while (ALooper_pollOnce(0, nullptr, &events,
                                reinterpret_cast<void**>(&source)) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested) return;
        }

        if (!s_app || !s_vg) continue;

        auto& win = s_app->getPlatformWindow();
        win.pollTimers();

        auto sz = win.getClientSize();
        glClearColor(0.98f, 0.98f, 0.98f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        float dpi = FluxAndroid_getDpiScale();
        nvgBeginFrame(s_vg, sz.width * dpi, sz.height * dpi, dpi);
        FluxAndroid_setVG(s_vg);
        nvgScale(s_vg, dpi, dpi);

        int logicalW = static_cast<int>(sz.width  / dpi);
        int logicalH = static_cast<int>(sz.height / dpi);

        GraphicsContext gc(sz.width, sz.height);
        Renderer::renderWidget(gc, s_app->getRoot().get(),
                               s_app->getFontCache());

        nvgEndFrame(s_vg);
        eglSwapBuffers(win.getEGLDisplay(), win.getEGLSurface());
    }
}
```


#### 6. AndroidManifest.xml

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

#### 6. Bundling images and assets

All files placed under `src/main/assets/` are packaged into the APK
automatically by Gradle. Reference them in code **without** a leading slash:
```
src/main/assets/images/logo.png  →  Image("images/logo.png")
src/main/assets/data/config.json →  "data/config.json"
```

For files outside the APK (downloads, camera photos, external storage),
use an absolute path with a leading slash — these go through the filesystem
directly and bypass `AAssetManager`:
```cpp
Image("/sdcard/Download/photo.jpg")
```

#### 7. Build and run

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

## Option B — Clone and build FluxUI directly (Windows)

Use this if you want to browse the source or run the built-in examples.

### 1. Clone the repo
```powershell
git clone https://github.com/Rosanchaudhary/flux.git
cd flux
```

### 2. Open Developer PowerShell for VS 2022

Search for **Developer PowerShell for VS 2022** in the Start menu and open it,
then navigate back:
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

## Using CMakePresets.json (Windows)
```powershell
cmake --preset msvc-debug
cmake --build build/msvc
```

Available presets: `msvc-debug`, `msvc-release`

---

## Troubleshooting

### Windows

**`cl.exe` not found / `CMAKE_CXX_COMPILER` could not be found`**

You are not in Developer PowerShell. Close your current terminal, search for
**Developer PowerShell for VS 2022** in the Start menu, and run the cmake
commands from there.

---

**`ninja: error: loading 'build.ninja': The system cannot find the file specified`**

You ran `cmake --build` before the configure step. Always configure first.
If the error persists, the build folder may be stale — delete it and start fresh:
```powershell
Remove-Item -Recurse -Force build/msvc
cmake -S . -B build/msvc -G Ninja -DCMAKE_BUILD_TYPE=Debug -DFLUX_BUILD_EXAMPLES=ON
cmake --build build/msvc
```

---

**`ninja: error: build.ninja:35: loading 'CMakeFiles\rules.ninja'`**

Stale cache. Delete and reconfigure:
```powershell
Remove-Item -Recurse -Force build/msvc
cmake -S . -B build/msvc -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/msvc
```

---

**`Generator Ninja does not support toolset specification`**

Remove the `"toolset"` line from `CMakePresets.json`. The `toolset` field is
only valid for Visual Studio generators, not Ninja.

---

**`Cannot find source file: external/Clipper2/...`**

Your `CMakeLists.txt` still has the old manual `add_library(clipper2 ...)` block.
Replace it with the FetchContent version. Clipper2 and glad are fetched
automatically — the `external/` folder is no longer needed.

---

**First configure takes a long time**

Normal — CMake is downloading Clipper2 via FetchContent. Subsequent configures
use the cached download in `build/msvc/_deps/`.

---

### Android

**`duplicate symbol: nvgCreateGLES2`**

`NANOVG_GLES2_IMPLEMENTATION` is being defined in more than one translation
unit. Make sure it is defined **only** inside `nanovg_gl_impl.c` and nowhere
else. Do not add it to any `target_compile_definitions` call.

---

**`undefined symbol: nvgCreateGLES2`**

`nanovg` is not being linked before `flux::flux` in `native-lib`'s
`target_link_libraries`. Order matters with static archives — `nanovg` must
come first:
```cmake
target_link_libraries(native-lib PRIVATE
        nanovg       # ← must be first
        flux::flux
)
```

---

**Fonts appear very small**

DPI scaling is not being applied. Make sure `FluxAndroid_setDpiScale()` is
called before `s_app->build()` in `handle_cmd`. The scale factor is
`AConfiguration_getDensity(config) / 160.0f`.

---

**Images not loading from assets/**

- Confirm the file is under `src/main/assets/` not `src/main/cpp/` or
  anywhere else.
- Use a path **without** a leading slash: `Image("images/logo.png")` not
  `Image("/images/logo.png")`.
- Make sure `FluxAndroid_setAssetManager(app->activity->assetManager)` is
  called at the top of `APP_CMD_INIT_WINDOW` before `build()`.

---

**App crashes immediately on launch**

Check logcat in Android Studio (View → Tool Windows → Logcat) filtered to
your package name. Common causes are a missing font file path in
`nvgCreateFont`, NVG being used before EGL is initialised, or a null
`AAssetManager`.


**App launches but shows a black screen with no UI**

Check that `android.app.lib_name` in `AndroidManifest.xml` exactly matches
the library name in `add_library()`. If the name doesn't match, Android loads
the wrong library (or none at all) and `android_main` is never called.

Also confirm the activity class is `android.app.NativeActivity` not a custom
Kotlin/Java activity — FluxUI uses the native activity model and does not
require any JVM-side code.