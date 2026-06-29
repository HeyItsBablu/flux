// flux_window_android.cpp
#ifdef __ANDROID__

#include "flux/flux_window.hpp"
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "FluxUI", __VA_ARGS__) 
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "FluxUI", __VA_ARGS__)

extern float FluxAndroid_getDpiScale();

// ── EGL state ─────────────────────────────────────────────────────────────────
struct PlatformWindow::EGLState
{
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
};

// ── EGL config attributes ─────────────────────────────────────────────────────


static const EGLint kAttribsES3[] = {
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_ALPHA_SIZE, 8,
    EGL_DEPTH_SIZE, 16,
    // EGL_STENCIL_SIZE removed — not needed by FluxGL
    EGL_NONE};

static const EGLint kAttribsES2[] = {
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_ALPHA_SIZE, 8,
    EGL_DEPTH_SIZE, 16,
    // EGL_STENCIL_SIZE removed — not needed by FluxGL
    EGL_NONE};

static const EGLint kCtxES3[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
static const EGLint kCtxES2[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};

// ── create ────────────────────────────────────────────────────────────────────
bool PlatformWindow::create(const std::string & /*title*/,
                            int /*w*/, int /*h*/,
                            AppInstance app, void * /*userData*/)
{
    androidApp = app;
    nativeHandle = app->window;
    eglState = new EGLState();

    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(dpy, nullptr, nullptr))
    {
        LOGE("eglInitialize failed");
        return false;
    }

    // Try ES3 config, fall back to ES2
    EGLConfig cfg;
    EGLint nCfg = 0;
    if (!eglChooseConfig(dpy, kAttribsES3, &cfg, 1, &nCfg) || nCfg == 0)
        if (!eglChooseConfig(dpy, kAttribsES2, &cfg, 1, &nCfg) || nCfg == 0)
        {
            LOGE("eglChooseConfig failed");
            return false;
        }

    EGLint fmt = 0;
    eglGetConfigAttrib(dpy, cfg, EGL_NATIVE_VISUAL_ID, &fmt);
    ANativeWindow_setBuffersGeometry(app->window, 0, 0, fmt);

    EGLSurface srf = eglCreateWindowSurface(dpy, cfg, app->window, nullptr);

    // Try ES3 context, fall back to ES2
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, kCtxES3);
    if (ctx == EGL_NO_CONTEXT)
    {
        LOGI("ES3 context failed, falling back to ES2");
        ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, kCtxES2);
    }

    if (!eglMakeCurrent(dpy, srf, srf, ctx))
    {
        LOGE("eglMakeCurrent failed");
        return false;
    }

    EGLint w = 0, h = 0;
    eglQuerySurface(dpy, srf, EGL_WIDTH, &w);
    eglQuerySurface(dpy, srf, EGL_HEIGHT, &h);

    float dpi = FluxAndroid_getDpiScale();
    eglState->display = dpy;
    eglState->surface = srf;
    eglState->context = ctx;
    cachedWidth = static_cast<int>(w / dpi);
    cachedHeight = static_cast<int>(h / dpi);

    glViewport(0, 0, w, h);
    LOGI("EGL ready %dx%d (logical %dx%d)", w, h, cachedWidth, cachedHeight);
    return true;
}

// ── reinitSurface ─────────────────────────────────────────────────────────────
void PlatformWindow::reinitSurface(ANativeWindow *window)
{
    if (!eglState)
        return;
    auto &s = *eglState;

    // Destroy old surface only — keep display and context alive
    if (s.surface != EGL_NO_SURFACE)
    {
        eglMakeCurrent(s.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(s.display, s.surface);
        s.surface = EGL_NO_SURFACE;
    }

    // Re-query config to create the new surface (same attribs, no stencil)
    EGLConfig cfg;
    EGLint nCfg = 0;
    if (!eglChooseConfig(s.display, kAttribsES3, &cfg, 1, &nCfg) || nCfg == 0)
        eglChooseConfig(s.display, kAttribsES2, &cfg, 1, &nCfg);

    EGLint fmt = 0;
    eglGetConfigAttrib(s.display, cfg, EGL_NATIVE_VISUAL_ID, &fmt);
    ANativeWindow_setBuffersGeometry(window, 0, 0, fmt);

    s.surface = eglCreateWindowSurface(s.display, cfg, window, nullptr);
    if (s.surface == EGL_NO_SURFACE)
    {
        LOGE("reinitSurface: eglCreateWindowSurface failed");
        return;
    }

    if (!eglMakeCurrent(s.display, s.surface, s.surface, s.context))
    {
        LOGE("reinitSurface: eglMakeCurrent failed");
        return;
    }

    EGLint w = 0, h = 0;
    eglQuerySurface(s.display, s.surface, EGL_WIDTH, &w);
    eglQuerySurface(s.display, s.surface, EGL_HEIGHT, &h);

    float dpi = FluxAndroid_getDpiScale();
    cachedWidth = static_cast<int>(w / dpi);
    cachedHeight = static_cast<int>(h / dpi);
    nativeHandle = window;

    glViewport(0, 0, w, h);
    LOGI("reinitSurface: %dx%d (logical %dx%d)", w, h, cachedWidth, cachedHeight);
}

// ── destroySurface ────────────────────────────────────────────────────────────
void PlatformWindow::destroySurface()
{
    if (!eglState || eglState->surface == EGL_NO_SURFACE)
        return;
    eglMakeCurrent(eglState->display,
                   EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(eglState->display, eglState->surface);
    eglState->surface = EGL_NO_SURFACE;
}

// ── destroy ───────────────────────────────────────────────────────────────────
void PlatformWindow::destroy()
{
    if (!eglState)
        return;
    auto &s = *eglState;
    if (s.display != EGL_NO_DISPLAY)
    {
        eglMakeCurrent(s.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (s.context != EGL_NO_CONTEXT)
            eglDestroyContext(s.display, s.context);
        if (s.surface != EGL_NO_SURFACE)
            eglDestroySurface(s.display, s.surface);
        eglTerminate(s.display);
    }
    delete eglState;
    eglState = nullptr;
    nativeHandle = nullptr;
}

// ── accessors ─────────────────────────────────────────────────────────────────
NativeWindow PlatformWindow::handle() const { return nativeHandle; }
bool PlatformWindow::isMouseCaptured() const { return mouseCapture; }

// ── run — no-op (android_main IS the loop) ───────────────────────────────────
int PlatformWindow::run() { return 0; }

// ── invalidate ────────────────────────────────────────────────────────────────
void PlatformWindow::invalidate() { dirty = true; }
void PlatformWindow::invalidateRect(int, int, int, int) { dirty = true; }

// ── software timers (polled each frame by android_main) ──────────────────────
void PlatformWindow::setTimer(TimerID id, int ms)
{
    androidTimers[id] = {ms,
                         platformTickCount() + static_cast<uint32_t>(ms)};
}
void PlatformWindow::killTimer(TimerID id)
{
    androidTimers.erase(id);
}

void PlatformWindow::pollTimers()
{
    uint32_t now = platformTickCount();
    for (auto &[id, entry] : androidTimers)
    {
        if (now >= entry.nextFireMs)
        {
            entry.nextFireMs = now + static_cast<uint32_t>(entry.intervalMs);
            if (callbacks.onTimer)
                callbacks.onTimer(id);
        }
    }
}

// ── size query ────────────────────────────────────────────────────────────────
void PlatformWindow::updateClientSize()
{
    if (!eglState || eglState->display == EGL_NO_DISPLAY)
        return;
    EGLint w = 0, h = 0;
    eglQuerySurface(eglState->display, eglState->surface, EGL_WIDTH, &w);
    eglQuerySurface(eglState->display, eglState->surface, EGL_HEIGHT, &h);
    float dpi = FluxAndroid_getDpiScale();
    cachedWidth = static_cast<int>(w / dpi);
    cachedHeight = static_cast<int>(h / dpi);
}

GraphicsContext PlatformWindow::getMeasureContext() const
{
    return GraphicsContext(cachedWidth, cachedHeight);
}

// ── touch input ───────────────────────────────────────────────────────────────
void PlatformWindow::handleAndroidEvent(const AInputEvent *event)
{
    if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION)
        return;

    int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;

    float dpi = FluxAndroid_getDpiScale();
    int mx = (int)(AMotionEvent_getX(event, 0) / dpi);
    int my = (int)(AMotionEvent_getY(event, 0) / dpi);

    switch (action)
    {
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
        if (action == AMOTION_EVENT_ACTION_CANCEL && callbacks.onMouseLeave)
            callbacks.onMouseLeave();
        break;
    }
}

// ── stubs ────────────────────────────────────────────────────────────────────
void PlatformWindow::captureMouseInput() { mouseCapture = true; }
void PlatformWindow::releaseMouseInput() { mouseCapture = false; }
void PlatformWindow::setResizeCursorH() {}
void PlatformWindow::setResizeCursorV() {}
void PlatformWindow::setDefaultCursor() {}
void PlatformWindow::startRenderLoop() {}

void PlatformWindow::setClipboardText(const std::string &) {}
std::string PlatformWindow::getClipboardText() { return {}; }

PlatformWindow::ScreenPoint PlatformWindow::clientToScreen(int x, int y) const { return {x, y}; }
PlatformWindow::ScreenPoint PlatformWindow::screenToClient(int x, int y) const { return {x, y}; }
PlatformWindow::ClientSize PlatformWindow::getClientSize() const { return {cachedWidth, cachedHeight}; }

bool PlatformWindow::valid() const { return eglState != nullptr; }

EGLDisplay PlatformWindow::getEGLDisplay() const
{
    return eglState ? eglState->display : EGL_NO_DISPLAY;
}
EGLSurface PlatformWindow::getEGLSurface() const
{
    return eglState ? eglState->surface : EGL_NO_SURFACE;
}
EGLContext PlatformWindow::getEGLContext() const
{
    return eglState ? eglState->context : EGL_NO_CONTEXT;
}

#endif // __ANDROID__