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
    fprintf(stderr, "[FLUX %s:%d] " fmt "\n", __func__, __LINE__,             \
            ##__VA_ARGS__);                                                    \
    fflush(stderr);                                                            \
  } while (0)

// ============================================================================
// FluxWin_markNeedsPaint — callable from any thread
// Uses the registered canvas repaint event type so it is never filtered
// by SDL's window-ID checks (unlike a zeroed SDL_WINDOWEVENT).
// ============================================================================
void FluxWin_markNeedsPaint() {
    SDL_Event e;
    SDL_zero(e);
    Uint32 t = CanvasWidget::repaintEventType();
    e.type      = (t != 0) ? t : SDL_USEREVENT;
    e.user.code = -1;       // -1 = "global repaint, no specific canvas"
    SDL_PushEvent(&e);
}

// ============================================================================
// CairoState
// Renders UI widgets into a CPU-side ARGB32 surface.
// IMPORTANT: SDL_GetWindowSurface() is incompatible with OpenGL windows.
// We allocate our own pixel buffer and wrap it with cairo_image_surface.
// ============================================================================
static Uint32 gFluxTimerEventType = 0;

struct PlatformWindow::CairoState {
    std::vector<uint8_t> pixels;      // owns the pixel data
    int                  width  = 0;
    int                  height = 0;
    cairo_surface_t*     cairoSurf = nullptr;
    cairo_t*             cr        = nullptr;

    // Rebuild at a new size. Tears down old resources first.
    bool rebuild(int w, int h) {
        teardown();
        if (w <= 0 || h <= 0) return false;

        width  = w;
        height = h;
        pixels.assign(static_cast<size_t>(w) * h * 4, 0);

        cairoSurf = cairo_image_surface_create_for_data(
            pixels.data(),
            CAIRO_FORMAT_ARGB32,
            w, h,
            w * 4);

        if (cairo_surface_status(cairoSurf) != CAIRO_STATUS_SUCCESS) {
            teardown();
            return false;
        }

        cr = cairo_create(cairoSurf);
        if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
            teardown();
            return false;
        }
        return true;
    }

    void teardown() {
        if (cr)       { cairo_destroy(cr);               cr        = nullptr; }
        if (cairoSurf){ cairo_surface_destroy(cairoSurf); cairoSurf = nullptr; }
        pixels.clear();
        width = height = 0;
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
// ============================================================================
bool PlatformWindow::create(const std::string& title, int width, int height,
                             AppInstance /*instance*/, void* /*userData*/) {

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    // Register all custom event types before any CanvasWidget is created.
    gFluxTimerEventType = SDL_RegisterEvents(1);
    CanvasWidget::initEventType();
    fluxInitUIThread();

    // ── Request GL 3.3 core profile ──────────────────────────────────────────
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,  1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,   24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,  8);   // NanoVG requires stencil
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

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

    // Use drawable (physical) pixels for all GL operations.
    // On HiDPI screens logical != physical pixels.
    SDL_GL_GetDrawableSize(nativeHandle, &cachedWidth, &cachedHeight);
    logicalWidth_  = width;
    logicalHeight_ = height;

    // ── GL context ────────────────────────────────────────────────────────────
    glContext_ = SDL_GL_CreateContext(nativeHandle);
    if (!glContext_) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(nativeHandle);
        nativeHandle = nullptr;
        SDL_Quit();
        return false;
    }

    SDL_GL_MakeCurrent(nativeHandle, glContext_);
    // vsync on by default — avoids busy-loop for continuous-redraw canvases.
    // Override with SDL_GL_SetSwapInterval(0) after create() if you need
    // manual timing.
    SDL_GL_SetSwapInterval(1);

    // ── GLAD ──────────────────────────────────────────────────────────────────
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

    // ── NanoVG ────────────────────────────────────────────────────────────────
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

    // ── Cairo CPU surface (for UI widget rendering) ────────────────────────────
    // Use logical dimensions for Cairo — the widget coordinate system is in
    // logical pixels. The NVG texture upload uses cachedWidth/cachedHeight
    // (physical) only for the GL side.
    cairoState = new CairoState();
    if (!cairoState->rebuild(logicalWidth_, logicalHeight_)) {
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

        // ── Event collection ──────────────────────────────────────────────────
        // SDL_WaitEventTimeout avoids blocking forever while still allowing
        // continuous-redraw canvases to animate smoothly. 16 ms ≈ 60 fps cap
        // when no events arrive and a canvas needs continuous redraw.
        bool hasEvent = (SDL_WaitEventTimeout(&e, 16) != 0);
        if (hasEvent)
            handleSDLEvent(e);

        // Drain all additional queued events
        while (SDL_PollEvent(&e))
            handleSDLEvent(e);

        // Check if any canvas needs a continuous repaint even with no events
        if (!dirty) {
            for (CanvasWidget* cw : canvasWidgets_) {
                if (cw && cw->isInitialized() && cw->needsRepaint()) {
                    dirty = true;
                    break;
                }
            }
        }

        // Process HTTP callbacks
        fluxProcessHttpEvents();

        // ── Composite and present ─────────────────────────────────────────────
        if (!dirty) continue;
        dirty = false;

        SDL_GL_MakeCurrent(nativeHandle, glContext_);

        // ── Pass 1: raw GL work for all canvas widgets ────────────────────────
        // Must happen BEFORE nvgBeginFrame. Each canvas runs its preRender()
        // (FBO shader passes, texture uploads, etc.) and restores GL state
        // before returning.
        for (CanvasWidget* cw : canvasWidgets_)
            if (cw && cw->isInitialized()) cw->preRenderPass();

        // Restore default framebuffer and full-window viewport before NVG.
        // preRenderPass() already does this internally but we do it once more
        // here as a safety net in case any surface forgot to restore.
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, cachedWidth, cachedHeight);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT |
                GL_DEPTH_BUFFER_BIT |
                GL_STENCIL_BUFFER_BIT);
        glEnable(GL_MULTISAMPLE);

        // ── Pass 2: NanoVG frame ──────────────────────────────────────────────
        // NVG works in logical pixels; pixel ratio accounts for HiDPI.
        float pixelRatio = (logicalWidth_ > 0)
                           ? float(cachedWidth) / float(logicalWidth_)
                           : 1.f;

        nvgBeginFrame(nvg_,
                      float(logicalWidth_),
                      float(logicalHeight_),
                      pixelRatio);

        // ── Layer 1: Cairo UI as a fullscreen NVG texture ─────────────────────
        if (cairoState && cairoState->cr) {
            // Paint all UI widgets into the Cairo context
            GraphicsContext ctx(cairoState->cr,
                                logicalWidth_, logicalHeight_);
            if (callbacks.onPaint)
                callbacks.onPaint(ctx, logicalWidth_, logicalHeight_);

            cairo_surface_flush(cairoState->cairoSurf);

            // Cairo produces BGRA (ARGB32 little-endian on x86).
            // NVG nvgCreateImageRGBA / nvgUpdateImage expect RGBA.
            // Swap R and B channels into a persistent conversion buffer.
            const uint8_t* src = cairoState->pixels.data();
            int pixelCount = logicalWidth_ * logicalHeight_;

            if (static_cast<int>(cairoPixelBuf_.size()) != pixelCount * 4)
                cairoPixelBuf_.resize(static_cast<size_t>(pixelCount) * 4);

            for (int i = 0; i < pixelCount; ++i) {
                cairoPixelBuf_[i*4+0] = src[i*4+2]; // R ← B
                cairoPixelBuf_[i*4+1] = src[i*4+1]; // G
                cairoPixelBuf_[i*4+2] = src[i*4+0]; // B ← R
                cairoPixelBuf_[i*4+3] = src[i*4+3]; // A
            }

            if (cairoTexture_ < 0) {
                cairoTexture_ = nvgCreateImageRGBA(
                    nvg_,
                    logicalWidth_, logicalHeight_,
                    NVG_IMAGE_PREMULTIPLIED,
                    cairoPixelBuf_.data());
            } else {
                nvgUpdateImage(nvg_, cairoTexture_, cairoPixelBuf_.data());
            }

            if (cairoTexture_ >= 0) {
                NVGpaint paint = nvgImagePattern(
                    nvg_,
                    0.f, 0.f,
                    float(logicalWidth_), float(logicalHeight_),
                    0.f, cairoTexture_, 1.f);
                nvgBeginPath(nvg_);
                nvgRect(nvg_, 0.f, 0.f,
                        float(logicalWidth_), float(logicalHeight_));
                nvgFillPaint(nvg_, paint);
                nvgFill(nvg_);
            }
        }

        // ── Layer 2: canvas widgets (NVG pass only) ───────────────────────────
        // Raw GL work already ran in Pass 1. Each widget scissors to its own
        // rect and draws via NanoVG into this shared frame.
        for (CanvasWidget* cw : canvasWidgets_)
            if (cw && cw->isInitialized()) cw->nvgRenderPass();

        nvgEndFrame(nvg_);
        SDL_GL_SwapWindow(nativeHandle);
    }

    return 0;
}

// ============================================================================
// Canvas registry helpers
// ============================================================================
void PlatformWindow::registerCanvas(CanvasWidget* c) {
    if (!c) return;
    canvasWidgets_.push_back(c);
    if (nvg_)
        c->setNVGContext(nvg_);
}

void PlatformWindow::unregisterCanvas(CanvasWidget* c) {
    if (capturedCanvas_ == c) capturedCanvas_ = nullptr;
    if (focusedCanvas_  == c) focusedCanvas_  = nullptr;
    canvasWidgets_.erase(
        std::remove(canvasWidgets_.begin(), canvasWidgets_.end(), c),
        canvasWidgets_.end());
}

CanvasWidget* PlatformWindow::hitTestCanvas(int sx, int sy) {
    // Iterate in reverse — widgets rendered last (on top) get priority.
    for (auto it = canvasWidgets_.rbegin(); it != canvasWidgets_.rend(); ++it) {
        CanvasWidget* cw = *it;
        if (!cw || !cw->isInitialized()) continue;
        if (cw->containsPoint(sx, sy)) return cw;
    }
    return nullptr;
}

// ============================================================================
// handleSDLEvent
// ============================================================================
void PlatformWindow::handleSDLEvent(const SDL_Event& e) {

    if (e.type == gFluxHttpEventType)
        return;

    // ── Repaint request from CanvasWidget::scheduleRepaint() ─────────────────
    if (e.type == CanvasWidget::repaintEventType()) {
        dirty = true;
        return;
    }

    switch (e.type) {

    // ── Quit ──────────────────────────────────────────────────────────────────
    case SDL_QUIT:
        running = false;
        break;

    // ── Mouse button down ─────────────────────────────────────────────────────
    case SDL_MOUSEBUTTONDOWN: {
        CanvasWidget* cw = hitTestCanvas(e.button.x, e.button.y);
        if (cw) {
            // Track which canvas owns the drag and logical keyboard focus.
            capturedCanvas_ = cw;
            focusedCanvas_  = cw;

            SDL_Event local = e;
            local.button.x -= cw->x;
            local.button.y -= cw->y;
            cw->handleSDLEvent(local);
            dirty = true;
            break;
        }
        // Click landed on Cairo UI — clear canvas focus.
        focusedCanvas_ = nullptr;

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
        // Use the captured canvas (if any) so drag-release outside the rect
        // is still delivered to the correct widget.
        CanvasWidget* cw = capturedCanvas_
                           ? capturedCanvas_
                           : hitTestCanvas(e.button.x, e.button.y);

        // Release capture on left or middle button up.
        if (e.button.button == SDL_BUTTON_LEFT ||
            e.button.button == SDL_BUTTON_MIDDLE)
            capturedCanvas_ = nullptr;

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
        // Deliver to the captured canvas first (handles drag outside rect).
        CanvasWidget* cw = capturedCanvas_
                           ? capturedCanvas_
                           : hitTestCanvas(e.motion.x, e.motion.y);
        if (cw) {
            SDL_Event local = e;
            local.motion.x -= cw->x;
            local.motion.y -= cw->y;
            cw->handleSDLEvent(local);
            dirty = true;
            break;
        }
        if (callbacks.onMouseMove)
            if (callbacks.onMouseMove(e.motion.x, e.motion.y))
                dirty = true;
        break;
    }

    // ── Mouse wheel ───────────────────────────────────────────────────────────
    case SDL_MOUSEWHEEL: {
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        CanvasWidget* cw = hitTestCanvas(mx, my);
        if (cw) {
            // Wheel events carry no position — canvas does its own translation.
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
        // Route to the canvas that has logical focus (last clicked), not the
        // one under the mouse cursor. This matches Win32 focus behaviour.
        if (focusedCanvas_) {
            focusedCanvas_->handleSDLEvent(e);
            dirty = true;
            break;
        }
        if (callbacks.onKeyDown && callbacks.onKeyDown(e.key.keysym.sym))
            dirty = true;
        break;
    }

    // ── Key up ────────────────────────────────────────────────────────────────
    case SDL_KEYUP: {
        if (focusedCanvas_) {
            focusedCanvas_->handleSDLEvent(e);
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
        case SDL_WINDOWEVENT_SIZE_CHANGED: {
            // Use drawable size (physical pixels) for GL.
            // Use logical size for Cairo and widget coordinates.
            SDL_GL_GetDrawableSize(nativeHandle, &cachedWidth, &cachedHeight);
            SDL_GetWindowSize(nativeHandle, &logicalWidth_, &logicalHeight_);

            // Rebuild Cairo CPU buffer at the new logical size.
            if (cairoState) {
                cairoState->rebuild(logicalWidth_, logicalHeight_);
            }

            // Invalidate the NVG Cairo texture — wrong size now.
            if (nvg_ && cairoTexture_ >= 0) {
                nvgDeleteImage(nvg_, cairoTexture_);
                cairoTexture_ = -1;
            }

            // Notify UI layer.
            if (callbacks.onResize && cairoState && cairoState->cr) {
                GraphicsContext ctx(cairoState->cr,
                                    logicalWidth_, logicalHeight_);
                callbacks.onResize(ctx, logicalWidth_, logicalHeight_);
            }

            // Notify all canvas widgets so they can update their viewport.
            for (CanvasWidget* cw : canvasWidgets_)
                if (cw) cw->onWindowResize(cachedWidth, cachedHeight);

            dirty = true;
            break;
        }

        case SDL_WINDOWEVENT_EXPOSED:
            dirty = true;
            break;

        case SDL_WINDOWEVENT_LEAVE:
            capturedCanvas_ = nullptr;
            for (CanvasWidget* cw : canvasWidgets_)
                if (cw) cw->onMouseLeave();
            if (callbacks.onMouseLeave)
                callbacks.onMouseLeave();
            break;

        case SDL_WINDOWEVENT_FOCUS_LOST:
            // Release canvas capture/focus when window loses focus.
            capturedCanvas_ = nullptr;
            focusedCanvas_  = nullptr;
            if (callbacks.onFocusLost)
                callbacks.onFocusLost();
            dirty = true;
            break;

        case SDL_WINDOWEVENT_FOCUS_GAINED:
            dirty = true;
            break;
        }
        break;

    // ── Timer / file-picker / other custom events ─────────────────────────────
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
    // Use the registered repaint event type — never a zeroed SDL_WINDOWEVENT
    // which can be silently dropped if windowID == 0.
    Uint32 t = CanvasWidget::repaintEventType();
    e.type      = (t != 0) ? t : SDL_USEREVENT;
    e.user.code = -1;
    SDL_PushEvent(&e);
}

void PlatformWindow::invalidateRect(int /*x*/, int /*y*/,
                                     int /*w*/, int /*h*/) {
    if (!dirty) {
        dirty = true;
        SDL_Event e;
        SDL_zero(e);
        Uint32 t = CanvasWidget::repaintEventType();
        e.type      = (t != 0) ? t : SDL_USEREVENT;
        e.user.code = -1;
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
    return { logicalWidth_, logicalHeight_ };
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
        return GraphicsContext(cairoState->cr, logicalWidth_, logicalHeight_);
    return GraphicsContext();
}

void PlatformWindow::updateClientSize() {
    if (nativeHandle) {
        SDL_GL_GetDrawableSize(nativeHandle, &cachedWidth, &cachedHeight);
        SDL_GetWindowSize(nativeHandle, &logicalWidth_, &logicalHeight_);
    }
}

#endif // __linux__