// flux_window_android.cpp
#ifdef __ANDROID__

#include "flux/flux_window.hpp"
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "FluxUI", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "FluxUI", __VA_ARGS__)


extern float FluxAndroid_getDpiScale();

// ── EGL state ─────────────────────────────────────────────────────────────────
struct PlatformWindow::EGLState {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
};

// ── create ────────────────────────────────────────────────────────────────────
bool PlatformWindow::create(const std::string& /*title*/,
                             int /*w*/, int /*h*/,
                             AppInstance app, void* /*userData*/) {
    androidApp  = app;
    nativeHandle = app->window;
    eglState     = new EGLState();

    const EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,   8, EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE,  8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };

    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(dpy, nullptr, nullptr)) {
        LOGE("eglInitialize failed"); return false;
    }

    EGLConfig cfg; EGLint nCfg = 0;
    if (!eglChooseConfig(dpy, attribs, &cfg, 1, &nCfg) || nCfg == 0) {
        LOGE("eglChooseConfig failed"); return false;
    }

    EGLint fmt = 0;
    eglGetConfigAttrib(dpy, cfg, EGL_NATIVE_VISUAL_ID, &fmt);
    ANativeWindow_setBuffersGeometry(app->window, 0, 0, fmt);

    EGLSurface srf = eglCreateWindowSurface(dpy, cfg, app->window, nullptr);
    const EGLint ctxA[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext   ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxA);

    if (!eglMakeCurrent(dpy, srf, srf, ctx)) {
        LOGE("eglMakeCurrent failed"); return false;
    }

    EGLint w = 0, h = 0;
    eglQuerySurface(dpy, srf, EGL_WIDTH,  &w);
    eglQuerySurface(dpy, srf, EGL_HEIGHT, &h);
    float dpi = FluxAndroid_getDpiScale();
    eglState->display = dpy;
    eglState->surface = srf;
    eglState->context = ctx;
    cachedWidth  = static_cast<int>(w / dpi);
    cachedHeight = static_cast<int>(h / dpi);

    glViewport(0, 0, w, h);
    LOGI("EGL ready %dx%d", w, h);
    return true;
}

// ── destroy ───────────────────────────────────────────────────────────────────
void PlatformWindow::destroy() {
    if (!eglState) return;
    auto& s = *eglState;
    if (s.display != EGL_NO_DISPLAY) {
        eglMakeCurrent(s.display,
                       EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (s.context != EGL_NO_CONTEXT)
            eglDestroyContext(s.display, s.context);
        if (s.surface != EGL_NO_SURFACE)
            eglDestroySurface(s.display, s.surface);
        eglTerminate(s.display);
    }
    delete eglState;
    eglState    = nullptr;
    nativeHandle = nullptr;
}

// ── run — no-op on Android (android_main IS the loop) ────────────────────────
int PlatformWindow::run() { return 0; }

// ── invalidate ────────────────────────────────────────────────────────────────
void PlatformWindow::invalidate()                          { dirty = true; }
void PlatformWindow::invalidateRect(int,int,int,int)       { dirty = true; }

// ── software timers (polled each frame by android_main) ──────────────────────
void PlatformWindow::setTimer(TimerID id, int ms) {
    androidTimers[id] = { ms,
        platformTickCount() + static_cast<uint32_t>(ms) };
}
void PlatformWindow::killTimer(TimerID id) {
    androidTimers.erase(id);
}

void PlatformWindow::pollTimers() {
    uint32_t now = platformTickCount();
    for (auto& [id, entry] : androidTimers) {
        if (now >= entry.nextFireMs) {
            entry.nextFireMs = now + static_cast<uint32_t>(entry.intervalMs);
            if (callbacks.onTimer) callbacks.onTimer(id);
        }
    }
}

// ── size query ────────────────────────────────────────────────────────────────
void PlatformWindow::updateClientSize() {
    if (!eglState || eglState->display == EGL_NO_DISPLAY) return;
    EGLint w = 0, h = 0;
    eglQuerySurface(eglState->display, eglState->surface, EGL_WIDTH,  &w);
    eglQuerySurface(eglState->display, eglState->surface, EGL_HEIGHT, &h);
    // Store logical pixels — physical / dpi
    float dpi = FluxAndroid_getDpiScale();
    cachedWidth  = static_cast<int>(w / dpi);
    cachedHeight = static_cast<int>(h / dpi);
}

GraphicsContext PlatformWindow::getMeasureContext() const {
    return GraphicsContext(cachedWidth, cachedHeight);
}

// ── touch input → FluxUI callbacks ───────────────────────────────────────────
// In flux_window_android.cpp, inside handleAndroidEvent or equivalent:

void PlatformWindow::handleAndroidEvent(const AInputEvent *event) {
    if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) return;

    int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;

    // ── Convert physical pixels → logical pixels to match widget layout ──
    float dpi = FluxAndroid_getDpiScale();
    int mx = (int)(AMotionEvent_getX(event, 0) / dpi);
    int my = (int)(AMotionEvent_getY(event, 0) / dpi);

    switch (action) {
        case AMOTION_EVENT_ACTION_DOWN:
            if (callbacks.onMouseDown && callbacks.onMouseDown(mx, my))
                dirty = true;
            break;
        case AMOTION_EVENT_ACTION_MOVE:
            if (callbacks.onMouseMove && callbacks.onMouseMove(mx, my))
                dirty = true;
            break;
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_CANCEL:
            if (callbacks.onMouseUp && callbacks.onMouseUp(mx, my))
                dirty = true;
            if (action == AMOTION_EVENT_ACTION_CANCEL)
                if (callbacks.onMouseLeave) callbacks.onMouseLeave();
            break;
    }
}

// ── stubs ────────────────────────────────────────────────────────────────────
void        PlatformWindow::startupGdiplus()               {}
void        PlatformWindow::captureMouseInput()            { mouseCapture = true;  }
void        PlatformWindow::releaseMouseInput()            { mouseCapture = false; }
void        PlatformWindow::setResizeCursorH()             {}
void        PlatformWindow::setResizeCursorV()             {}
void        PlatformWindow::setDefaultCursor()             {}
void        PlatformWindow::setClipboardText(const std::string&) {}
std::string PlatformWindow::getClipboardText()             { return {}; }

PlatformWindow::ScreenPoint
PlatformWindow::clientToScreen(int x, int y) const { return {x, y}; }
PlatformWindow::ScreenPoint
PlatformWindow::screenToClient(int x, int y) const { return {x, y}; }
PlatformWindow::ClientSize
PlatformWindow::getClientSize() const { return {cachedWidth, cachedHeight}; }

EGLDisplay PlatformWindow::getEGLDisplay() const {
    return eglState ? eglState->display : EGL_NO_DISPLAY;
}

EGLSurface PlatformWindow::getEGLSurface() const {
    return eglState ? eglState->surface : EGL_NO_SURFACE;
}
EGLContext PlatformWindow::getEGLContext() const {
    return eglState ? eglState->context : EGL_NO_CONTEXT;
}

#endif // __ANDROID__