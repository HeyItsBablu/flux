// flux_window_linux.cpp
#if defined(__linux__) && !defined(__ANDROID__)

#include "flux/flux_http_platform.hpp"
#include "flux/flux_window.hpp"
#include "flux/widgets/flux_file_picker.hpp"


#include "flux/platform/linux/flux_canvas_linux.hpp"

#include <SDL2/SDL.h>
#include <cairo/cairo.h>
#include <glad/glad.h>
#include <nanovg.h>
#include <nanovg_gl.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

// ============================================================================
// DEBUG MACRO
// ============================================================================
#define FLUX_DBG(fmt, ...)                                                     \
  do {                                                                         \
    fprintf(stderr, "[FLUX %s:%d] " fmt "\n", __func__, __LINE__,              \
            ##__VA_ARGS__);                                                    \
    fflush(stderr);                                                            \
  } while (0)

// ============================================================================
// FluxWin_markNeedsPaint  — callable from any thread
// ============================================================================
void FluxWin_markNeedsPaint() {
    SDL_Event e;
    SDL_zero(e);
    e.type         = SDL_WINDOWEVENT;
    e.window.event = SDL_WINDOWEVENT_EXPOSED;
    SDL_PushEvent(&e);
}

// ============================================================================
// CairoState
// Renders UI widgets into a CPU-side ARGB32 surface.
// Pixels are uploaded to a NVG texture each dirty frame.
// ============================================================================
static Uint32 gFluxTimerEventType = 0;

struct PlatformWindow::CairoState {
    SDL_Surface*    sdlSurface = nullptr;
    cairo_surface_t* cairoSurf = nullptr;
    cairo_t*         cr        = nullptr;


    bool rebuild(SDL_Window* win) {
        teardown();


        sdlSurface = SDL_GetWindowSurface(win);
        if (!sdlSurface)
            return false;

        cairoSurf = cairo_image_surface_create_for_data(
            static_cast<unsigned char*>(sdlSurface->pixels),
            CAIRO_FORMAT_ARGB32,
            sdlSurface->w,
            sdlSurface->h,
            sdlSurface->pitch);

        if (cairo_surface_status(cairoSurf) != CAIRO_STATUS_SUCCESS) {
            teardown();
            return false;
        }

        cr = cairo_create(cairoSurf);
        return cairo_status(cr) == CAIRO_STATUS_SUCCESS;
    }

    void teardown() {
        if (cr)       { cairo_destroy(cr);           cr        = nullptr; }
        if (cairoSurf){ cairo_surface_destroy(cairoSurf); cairoSurf = nullptr; }
        sdlSurface = nullptr;   // owned by SDL, not us
    }

    ~CairoState() { teardown(); }
};

// ============================================================================
// SDL timer callback
// ============================================================================
static Uint32 sdlTimerCallback(Uint32 interval, void* param) {
    SDL_Event e;
    SDL_zero(e);
    e.type      = gFluxTimerEventType;
    e.user.code = static_cast<Sint32>(reinterpret_cast<uintptr_t>(param));
    SDL_PushEvent(&e);
    return interval;
}

// ============================================================================
// PlatformWindow::create
// Creates ONE OpenGL window. Initialises GLAD and NanoVG here so the
// compositor and every CanvasWidget share a single context.
// Cairo is still used for UI widget rendering but its output is composited
// as a GL texture rather than blitted directly to the window surface.
// ============================================================================
bool PlatformWindow::create(const std::string& title, int width, int height,
                             AppInstance /*instance*/, void* /*userData*/) {

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    gFluxTimerEventType = SDL_RegisterEvents(1);
    fluxInitUIThread();

    // ── Request GL 3.3 core profile ──────────────────────────────────────
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,  1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,   24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,  8);   // NanoVG requires stencil
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

    // ── Single window — OpenGL + resizable ───────────────────────────────
    nativeHandle = SDL_CreateWindow(
        title.c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

    if (!nativeHandle) {
        // Retry without MSAA (some VMs / drivers don't support it)
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        nativeHandle = SDL_CreateWindow(
            title.c_str(),
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            width, height,
            SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    }

    if (!nativeHandle) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    cachedWidth  = width;
    cachedHeight = height;

    // ── GL context ────────────────────────────────────────────────────────
    glContext_ = SDL_GL_CreateContext(nativeHandle);
    if (!glContext_) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(nativeHandle);
        nativeHandle = nullptr;
        SDL_Quit();
        return false;
    }

    SDL_GL_MakeCurrent(nativeHandle, glContext_);
    SDL_GL_SetSwapInterval(0);   // manual repaint — no vsync

    // ── GLAD ──────────────────────────────────────────────────────────────
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        fprintf(stderr, "PlatformWindow: gladLoadGLLoader failed\n");
        SDL_GL_DeleteContext(glContext_);
        glContext_ = nullptr;
        SDL_DestroyWindow(nativeHandle);
        nativeHandle = nullptr;
        SDL_Quit();
        return false;
    }

    fprintf(stderr, "GL: %s | GLSL: %s | %s\n",
            (const char*)glGetString(GL_VERSION),
            (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION),
            (const char*)glGetString(GL_RENDERER));

    // ── NanoVG ────────────────────────────────────────────────────────────
    nvg_ = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    if (!nvg_) {
        fprintf(stderr, "PlatformWindow: nvgCreateGL3 failed\n");
        SDL_GL_DeleteContext(glContext_);
        glContext_ = nullptr;
        SDL_DestroyWindow(nativeHandle);
        nativeHandle = nullptr;
        SDL_Quit();
        return false;
    }

    // Register fonts that NVG will use for scrollbars, canvas text, etc.
    // Falls back gracefully if a font file is missing.
    auto regFont = [this](const char* name, const char* path) {
        if (nvgCreateFont(nvg_, name, path) < 0)
            fprintf(stderr, "PlatformWindow: font '%s' not found at '%s'\n",
                    name, path);
    };
    // DejaVu — present on virtually all Linux desktops
    regFont("sans",             "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    regFont("sans-bold",        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf");
    regFont("sans-italic",      "/usr/share/fonts/truetype/dejavu/DejaVuSans-Oblique.ttf");
    regFont("sans-bold-italic", "/usr/share/fonts/truetype/dejavu/DejaVuSans-BoldOblique.ttf");
    regFont("mono",             "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf");
    regFont("mono-bold",        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf");
    // Liberation — common alternative (Fedora, RHEL, etc.)
    regFont("sans",             "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf");
    regFont("sans-bold",        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf");

    // ── Cairo — still used for UI widgets, output goes to CPU buffer ──────
    cairoState = new CairoState();
    if (!cairoState->rebuild(nativeHandle)) {
        fprintf(stderr, "PlatformWindow: CairoState::rebuild failed "
                        "(UI will be blank, GL canvas still works)\n");
    }

    running = true;
    return true;
}

// ============================================================================
// PlatformWindow::destroy
// ============================================================================
void PlatformWindow::destroy() {
    for (auto& [id, sdlId] : sdlTimerMap)
        SDL_RemoveTimer(sdlId);
    sdlTimerMap.clear();

    fluxShutdownHttpQueue();

    // Release Cairo texture handle before destroying NVG
    if (nvg_ && cairoTexture_ >= 0) {
        nvgDeleteImage(nvg_, cairoTexture_);
        cairoTexture_ = -1;
    }

    delete cairoState;
    cairoState = nullptr;

    if (nvg_) {
        nvgDeleteGL3(nvg_);
        nvg_ = nullptr;
    }
    if (glContext_) {
        SDL_GL_DeleteContext(glContext_);
        glContext_ = nullptr;
    }
    if (nativeHandle) {
        SDL_DestroyWindow(nativeHandle);
        nativeHandle = nullptr;
    }

    SDL_Quit();
}

// ============================================================================
// PlatformWindow::run
// ============================================================================
int PlatformWindow::run() {
    SDL_Event e;

    while (running) {

        if (SDL_WaitEvent(&e) == 0)
            continue;

        handleSDLEvent(e);

        // Drain additional queued events
        while (SDL_PollEvent(&e))
            handleSDLEvent(e);

        // Process HTTP callbacks
        fluxProcessHttpEvents();

        // ── Composite and present ─────────────────────────────────────────
        if (dirty) {
            dirty = false;

            SDL_GL_MakeCurrent(nativeHandle, glContext_);

            glViewport(0, 0, cachedWidth, cachedHeight);
            glClearColor(0.f, 0.f, 0.f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT |
                    GL_DEPTH_BUFFER_BIT |
                    GL_STENCIL_BUFFER_BIT);
            glEnable(GL_MULTISAMPLE);

            nvgBeginFrame(nvg_,
                          float(cachedWidth),
                          float(cachedHeight),
                          1.f);

            // ── Layer 1: Cairo UI rendered as a fullscreen NVG texture ────
            if (cairoState && cairoState->cr) {

                if (SDL_MUSTLOCK(cairoState->sdlSurface))
                    SDL_LockSurface(cairoState->sdlSurface);

                // Let all UI widgets paint into the Cairo context
                GraphicsContext ctx(cairoState->cr,
                                    cachedWidth, cachedHeight);
                if (callbacks.onPaint)
                    callbacks.onPaint(ctx, cachedWidth, cachedHeight);

                cairo_surface_flush(cairoState->cairoSurf);

                if (SDL_MUSTLOCK(cairoState->sdlSurface))
                    SDL_UnlockSurface(cairoState->sdlSurface);

                // Cairo produces BGRA (ARGB32 little-endian).
                // NVG expects RGBA. Swap channels into a persistent buffer.
                const uint8_t* src = static_cast<const uint8_t*>(
                    cairoState->sdlSurface->pixels);
                int pixelCount = cachedWidth * cachedHeight;

                if (static_cast<int>(cairoPixelBuf_.size()) != pixelCount * 4)
                    cairoPixelBuf_.resize(pixelCount * 4);

                for (int i = 0; i < pixelCount; ++i) {
                    cairoPixelBuf_[i*4+0] = src[i*4+2]; // R ← B
                    cairoPixelBuf_[i*4+1] = src[i*4+1]; // G
                    cairoPixelBuf_[i*4+2] = src[i*4+0]; // B ← R
                    cairoPixelBuf_[i*4+3] = src[i*4+3]; // A
                }

                // Create NVG image on first use; update on subsequent frames
                if (cairoTexture_ < 0) {
                    cairoTexture_ = nvgCreateImageRGBA(
                        nvg_,
                        cachedWidth, cachedHeight,
                        NVG_IMAGE_PREMULTIPLIED,
                        cairoPixelBuf_.data());
                } else {
                    nvgUpdateImage(nvg_, cairoTexture_,
                                   cairoPixelBuf_.data());
                }

                // Draw Cairo layer as a fullscreen quad
                if (cairoTexture_ >= 0) {
                    NVGpaint paint = nvgImagePattern(
                        nvg_,
                        0.f, 0.f,
                        float(cachedWidth), float(cachedHeight),
                        0.f, cairoTexture_, 1.f);
                    nvgBeginPath(nvg_);
                    nvgRect(nvg_, 0.f, 0.f,
                            float(cachedWidth), float(cachedHeight));
                    nvgFillPaint(nvg_, paint);
                    nvgFill(nvg_);
                }
            }

            // ── Layer 2: Canvas widgets ───────────────────────────────────
            // Each widget scissors to its own rect and draws its surface
            // plus scrollbars into this shared NVG frame.
            for (CanvasWidget* cw : canvasWidgets_) {
                if (cw && cw->isInitialized())
                    cw->tickAndRender();
            }

            nvgEndFrame(nvg_);
            SDL_GL_SwapWindow(nativeHandle);
        }
    }

    return 0;
}

// ============================================================================
// Canvas registry helpers
// ============================================================================
void PlatformWindow::registerCanvas(CanvasWidget* c) {
    if (!c) return;
    canvasWidgets_.push_back(c);
    // Give the widget the shared NVG context immediately if GL is ready
    if (nvg_)
        c->setNVGContext(nvg_);
}

void PlatformWindow::unregisterCanvas(CanvasWidget* c) {
    canvasWidgets_.erase(
        std::remove(canvasWidgets_.begin(), canvasWidgets_.end(), c),
        canvasWidgets_.end());
}

// Returns the topmost CanvasWidget whose screen rect contains (sx, sy),
// or nullptr if the point is over pure Cairo UI.
CanvasWidget* PlatformWindow::hitTestCanvas(int sx, int sy) {
    // Iterate in reverse so widgets rendered last (on top) get events first
    for (auto it = canvasWidgets_.rbegin(); it != canvasWidgets_.rend(); ++it) {
        CanvasWidget* cw = *it;
        if (!cw || !cw->isInitialized()) continue;
        if (sx >= cw->x && sx < cw->x + cw->width &&
            sy >= cw->y && sy < cw->y + cw->height)
            return cw;
    }
    return nullptr;
}

// ============================================================================
// handleSDLEvent
// ============================================================================
void PlatformWindow::handleSDLEvent(const SDL_Event& e) {

    if (e.type == gFluxHttpEventType)
        return;

    switch (e.type) {

    // ── Quit ─────────────────────────────────────────────────────────────────
    case SDL_QUIT:
        running = false;
        break;

    // ── Mouse button down ─────────────────────────────────────────────────────
    case SDL_MOUSEBUTTONDOWN: {
        CanvasWidget* cw = hitTestCanvas(e.button.x, e.button.y);
        if (cw) {
            SDL_Event local = e;
            local.button.x -= cw->x;
            local.button.y -= cw->y;
            cw->handleSDLEvent(local);
            dirty = true;
            break;
        }
        if (e.button.button == SDL_BUTTON_LEFT && callbacks.onMouseDown)
            if (callbacks.onMouseDown(e.button.x, e.button.y))
                dirty = true;
        if (e.button.button == SDL_BUTTON_RIGHT && callbacks.onRightClick)
            if (callbacks.onRightClick(e.button.x, e.button.y))
                dirty = true;
        break;
    }

    // ── Mouse button up ───────────────────────────────────────────────────────
    case SDL_MOUSEBUTTONUP: {
        CanvasWidget* cw = hitTestCanvas(e.button.x, e.button.y);
        if (cw) {
            SDL_Event local = e;
            local.button.x -= cw->x;
            local.button.y -= cw->y;
            cw->handleSDLEvent(local);
            dirty = true;
            break;
        }
        if (e.button.button == SDL_BUTTON_LEFT && callbacks.onMouseUp)
            if (callbacks.onMouseUp(e.button.x, e.button.y))
                dirty = true;
        break;
    }

    // ── Mouse motion ──────────────────────────────────────────────────────────
    case SDL_MOUSEMOTION: {
        CanvasWidget* cw = hitTestCanvas(e.motion.x, e.motion.y);
        if (cw) {
            SDL_Event local = e;
            local.motion.x -= cw->x;
            local.motion.y -= cw->y;
            cw->handleSDLEvent(local);
            dirty = true;
            break;
        }
        if (callbacks.onMouseMove) {
            if (callbacks.onMouseMove(e.motion.x, e.motion.y))
                dirty = true;
        }
        break;
    }

    // ── Mouse wheel ───────────────────────────────────────────────────────────
    case SDL_MOUSEWHEEL: {
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        CanvasWidget* cw = hitTestCanvas(mx, my);
        if (cw) {
            // Wheel events carry no screen position so no translation needed
            cw->handleSDLEvent(e);
            dirty = true;
            break;
        }
        int delta = e.wheel.y * WHEEL_DELTA;
        if (callbacks.onMouseWheel && callbacks.onMouseWheel(delta))
            dirty = true;
        break;
    }

    // ── Key down ──────────────────────────────────────────────────────────────
    case SDL_KEYDOWN: {
        // Forward to whichever canvas the mouse is currently over;
        // fall through to UI callbacks if no canvas is hit.
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        CanvasWidget* cw = hitTestCanvas(mx, my);
        if (cw) {
            cw->handleSDLEvent(e);
            dirty = true;
            break;
        }
        if (callbacks.onKeyDown && callbacks.onKeyDown(e.key.keysym.sym))
            dirty = true;
        break;
    }

    // ── Key up ────────────────────────────────────────────────────────────────
    case SDL_KEYUP: {
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        CanvasWidget* cw = hitTestCanvas(mx, my);
        if (cw) {
            cw->handleSDLEvent(e);
            break;
        }
        break;
    }

    // ── Text input ────────────────────────────────────────────────────────────
    case SDL_TEXTINPUT:
        if (callbacks.onChar && e.text.text[0] != '\0') {
            unsigned char b0 = static_cast<unsigned char>(e.text.text[0]);
            wchar_t ch = 0;
            if (b0 < 0x80) {
                ch = static_cast<wchar_t>(b0);
            } else if ((b0 & 0xE0) == 0xC0 && e.text.text[1]) {
                ch = static_cast<wchar_t>(
                    ((b0 & 0x1F) << 6) |
                    (static_cast<unsigned char>(e.text.text[1]) & 0x3F));
            } else if ((b0 & 0xF0) == 0xE0 &&
                       e.text.text[1] && e.text.text[2]) {
                ch = static_cast<wchar_t>(
                    ((b0 & 0x0F) << 12) |
                    ((static_cast<unsigned char>(e.text.text[1]) & 0x3F) << 6) |
                    (static_cast<unsigned char>(e.text.text[2]) & 0x3F));
            }
            if (ch && callbacks.onChar(ch))
                dirty = true;
        }
        break;

    // ── Window events ─────────────────────────────────────────────────────────
    case SDL_WINDOWEVENT:
        switch (e.window.event) {

        case SDL_WINDOWEVENT_RESIZED:
        case SDL_WINDOWEVENT_SIZE_CHANGED:
            cachedWidth  = e.window.data1;
            cachedHeight = e.window.data2;

            // Rebuild Cairo CPU surface at the new size
            if (cairoState && !cairoState->rebuild(nativeHandle)) {
                dirty = true;
                break;
            }

            // Invalidate the NVG Cairo texture — will be recreated at new size
            if (nvg_ && cairoTexture_ >= 0) {
                nvgDeleteImage(nvg_, cairoTexture_);
                cairoTexture_ = -1;
            }

            // Notify UI layer
            if (callbacks.onResize && cairoState && cairoState->cr) {
                GraphicsContext ctx(cairoState->cr,
                                    cachedWidth, cachedHeight);
                callbacks.onResize(ctx, cachedWidth, cachedHeight);
            }

            // Notify all canvas widgets so they can update their viewport
            for (CanvasWidget* cw : canvasWidgets_)
                if (cw) cw->onWindowResize(cachedWidth, cachedHeight);

            dirty = true;
            break;

        case SDL_WINDOWEVENT_EXPOSED:
            dirty = true;
            break;

        case SDL_WINDOWEVENT_LEAVE:
            // Notify all canvases that the mouse has left the window
            for (CanvasWidget* cw : canvasWidgets_)
                if (cw) cw->onMouseLeave();
            if (callbacks.onMouseLeave)
                callbacks.onMouseLeave();
            break;

        case SDL_WINDOWEVENT_FOCUS_LOST:
            if (callbacks.onFocusLost)
                callbacks.onFocusLost();
            dirty = true;
            break;

        case SDL_WINDOWEVENT_FOCUS_GAINED:
            dirty = true;
            break;
        }
        break;

    // ── Timer / file-picker / custom events ───────────────────────────────────
    default:
        if (e.type == gFluxTimerEventType) {
            if (e.user.code == kFluxFilePickerEvent) {
                fluxFilePickerDispatchSDLEvent(e);
                dirty = true;
            } else {
                if (callbacks.onTimer)
                    callbacks.onTimer(static_cast<TimerID>(e.user.code));
                dirty = true;
            }
        }
        break;
    }
}

// ============================================================================
// invalidate / invalidateRect
// ============================================================================
void PlatformWindow::invalidate() {
    dirty = true;
    SDL_Event e;
    SDL_zero(e);
    e.type         = SDL_WINDOWEVENT;
    e.window.event = SDL_WINDOWEVENT_EXPOSED;
    SDL_PushEvent(&e);
}

void PlatformWindow::invalidateRect(int /*x*/, int /*y*/,
                                     int /*w*/, int /*h*/) {
    if (!dirty) {
        dirty = true;
        SDL_Event e;
        SDL_zero(e);
        e.type         = SDL_WINDOWEVENT;
        e.window.event = SDL_WINDOWEVENT_EXPOSED;
        SDL_PushEvent(&e);
    }
}

// ============================================================================
// setTimer / killTimer
// ============================================================================
void PlatformWindow::setTimer(TimerID id, int ms) {
    killTimer(id);
    SDL_TimerID sdlId = SDL_AddTimer(
        static_cast<Uint32>(ms),
        sdlTimerCallback,
        reinterpret_cast<void*>(static_cast<uintptr_t>(id)));
    if (sdlId != 0)
        sdlTimerMap[id] = sdlId;
}

void PlatformWindow::killTimer(TimerID id) {
    auto it = sdlTimerMap.find(id);
    if (it != sdlTimerMap.end()) {
        SDL_RemoveTimer(it->second);
        sdlTimerMap.erase(it);
    }
}

// ============================================================================
// Clipboard / mouse capture / coords / cursors / GDI stub
// ============================================================================
void PlatformWindow::setClipboardText(const std::string& text) {
    SDL_SetClipboardText(text.c_str());
}

std::string PlatformWindow::getClipboardText() {
    char* raw = SDL_GetClipboardText();
    std::string s = raw ? raw : "";
    SDL_free(raw);
    return s;
}

void PlatformWindow::captureMouseInput() {
    SDL_CaptureMouse(SDL_TRUE);
    mouseCapture = true;
}
void PlatformWindow::releaseMouseInput() {
    SDL_CaptureMouse(SDL_FALSE);
    mouseCapture = false;
}

PlatformWindow::ScreenPoint
PlatformWindow::clientToScreen(int cx, int cy) const {
    int wx = 0, wy = 0;
    if (nativeHandle)
        SDL_GetWindowPosition(nativeHandle, &wx, &wy);
    return { wx + cx, wy + cy };
}

PlatformWindow::ScreenPoint
PlatformWindow::screenToClient(int sx, int sy) const {
    int wx = 0, wy = 0;
    if (nativeHandle)
        SDL_GetWindowPosition(nativeHandle, &wx, &wy);
    return { sx - wx, sy - wy };
}

PlatformWindow::ClientSize PlatformWindow::getClientSize() const {
    return { cachedWidth, cachedHeight };
}

void PlatformWindow::setResizeCursorH() {
    static SDL_Cursor* c = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
    if (c) SDL_SetCursor(c);
}
void PlatformWindow::setResizeCursorV() {
    static SDL_Cursor* c = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS);
    if (c) SDL_SetCursor(c);
}
void PlatformWindow::setDefaultCursor() {
    static SDL_Cursor* c = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    if (c) SDL_SetCursor(c);
}

void PlatformWindow::startupGdiplus() {}   // no-op on Linux

GraphicsContext PlatformWindow::getMeasureContext() const {
    if (cairoState && cairoState->cr)
        return GraphicsContext(cairoState->cr, cachedWidth, cachedHeight);
    return GraphicsContext();
}

void PlatformWindow::updateClientSize() {
    if (nativeHandle)
        SDL_GetWindowSize(nativeHandle, &cachedWidth, &cachedHeight);
}

#endif // __linux__