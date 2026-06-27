// flux_window_linux.cpp
#if defined(__linux__) && !defined(__ANDROID__)

#include "flux/flux_http_platform.hpp"
#include "flux/flux_window.hpp"
#include "flux/widgets/flux_file_picker.hpp"
#include "flux/flux_canvas.hpp"

#include <SDL2/SDL.h>
#include <cairo/cairo.h>
#include <glad/glad.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

// ============================================================================
// DEBUG MACRO
// ============================================================================
#define FLUX_DBG(fmt, ...) \
    do { fprintf(stderr, "[FLUX %s:%d] " fmt "\n", \
                 __func__, __LINE__, ##__VA_ARGS__); fflush(stderr); } while(0)

// ============================================================================
// FluxWin_markNeedsPaint — callable from any thread
// ============================================================================
void FluxWin_markNeedsPaint() {
    SDL_Event e;
    SDL_zero(e);
    Uint32 t    = CanvasWidget::repaintEventType();
    e.type      = (t != 0) ? t : SDL_USEREVENT;
    e.user.code = -1;
    SDL_PushEvent(&e);
}

// ============================================================================
// CairoState — CPU-side ARGB32 surface for UI widget rendering
// ============================================================================
static Uint32 gFluxTimerEventType = 0;

struct PlatformWindow::CairoState {
    std::vector<uint8_t> pixels;
    int                  width  = 0;
    int                  height = 0;
    cairo_surface_t*     cairoSurf = nullptr;
    cairo_t*             cr        = nullptr;

    bool rebuild(int w, int h) {
        teardown();
        if (w <= 0 || h <= 0) return false;
        width = w; height = h;
        pixels.assign(static_cast<size_t>(w) * h * 4, 0);
        cairoSurf = cairo_image_surface_create_for_data(
            pixels.data(), CAIRO_FORMAT_ARGB32, w, h, w * 4);
        if (cairo_surface_status(cairoSurf) != CAIRO_STATUS_SUCCESS) { teardown(); return false; }
        cr = cairo_create(cairoSurf);
        if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) { teardown(); return false; }
        return true;
    }

    void teardown() {
        if (cr)       { cairo_destroy(cr);                cr        = nullptr; }
        if (cairoSurf){ cairo_surface_destroy(cairoSurf); cairoSurf = nullptr; }
        pixels.clear();
        width = height = 0;
    }

    ~CairoState() { teardown(); }
};

// ============================================================================
// Cairo UI — flat-colour fullscreen quad shader
// Composites the Cairo UI overlay (logical-pixel texture) over the GL scene.
// ============================================================================

static const char* kCairoVert = R"GLSL(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
out vec2 vUV;
void main(){ vUV=aUV; gl_Position=vec4(aPos,0.0,1.0); }
)GLSL";

static const char* kCairoFrag = R"GLSL(
#version 330 core
in  vec2 vUV;
out vec4 fragColor;
uniform sampler2D uTex;
void main(){ fragColor = texture(uTex, vUV); }
)GLSL";

struct CairoCompositor {
    GLuint prog   = 0;
    GLuint vao    = 0;
    GLuint vbo    = 0;
    GLuint tex    = 0;
    int    texW   = 0;
    int    texH   = 0;

    bool init() {
        static const float verts[] = {
            -1.f, -1.f,  0.f, 1.f,
             1.f, -1.f,  1.f, 1.f,
             1.f,  1.f,  1.f, 0.f,
             1.f,  1.f,  1.f, 0.f,
            -1.f,  1.f,  0.f, 0.f,
            -1.f, -1.f,  0.f, 1.f
        };

        auto compile = [](GLenum t, const char* src) -> GLuint {
            GLuint s = glCreateShader(t);
            glShaderSource(s, 1, &src, nullptr);
            glCompileShader(s);
            GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
            if (!ok) { glDeleteShader(s); return 0; }
            return s;
        };
        GLuint vs = compile(GL_VERTEX_SHADER,   kCairoVert);
        GLuint fs = compile(GL_FRAGMENT_SHADER, kCairoFrag);
        if (!vs || !fs) return false;
        prog = glCreateProgram();
        glAttachShader(prog, vs); glAttachShader(prog, fs);
        glLinkProgram(prog);
        glDeleteShader(vs); glDeleteShader(fs);
        GLint ok = 0; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok) { glDeleteProgram(prog); prog = 0; return false; }

        glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
        glBindVertexArray(0);

        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        return true;
    }

    // Upload Cairo BGRA→RGBA and draw over the GL scene.
    // cairoBGRA / w / h are in LOGICAL pixels — the texture is stretched over
    // the full NDC quad so it naturally fills the physical viewport.
    void draw(const uint8_t* cairoBGRA, int w, int h) {
        if (!prog || !w || !h || !cairoBGRA) return;

        static std::vector<uint8_t> rgba;
        int n = w * h;
        rgba.resize(size_t(n) * 4);
        for (int i = 0; i < n; ++i) {
            rgba[i*4+0] = cairoBGRA[i*4+2];
            rgba[i*4+1] = cairoBGRA[i*4+1];
            rgba[i*4+2] = cairoBGRA[i*4+0];
            rgba[i*4+3] = cairoBGRA[i*4+3];
        }

        glBindTexture(GL_TEXTURE_2D, tex);
        if (w != texW || h != texH) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
            texW = w; texH = h;
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
                            GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        }
        glBindTexture(GL_TEXTURE_2D, 0);

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        glUseProgram(prog);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        glUniform1i(glGetUniformLocation(prog, "uTex"), 0);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);

        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    void destroy() {
        if (prog) { glDeleteProgram(prog); prog = 0; }
        if (vao)  { glDeleteVertexArrays(1, &vao); vao = 0; }
        if (vbo)  { glDeleteBuffers(1, &vbo); vbo = 0; }
        if (tex)  { glDeleteTextures(1, &tex); tex = 0; }
        texW = texH = 0;
    }
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

    gFluxTimerEventType = SDL_RegisterEvents(1);
    CanvasWidget::initEventType();
    fluxInitUIThread();

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,  SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,  24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

    nativeHandle = SDL_CreateWindow(title.c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
        | SDL_WINDOW_ALLOW_HIGHDPI);  // <-- enable HiDPI drawable size

    if (!nativeHandle) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        nativeHandle = SDL_CreateWindow(title.c_str(),
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            width, height,
            SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
            | SDL_WINDOW_ALLOW_HIGHDPI);
    }
    if (!nativeHandle) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit(); return false;
    }

    // Physical drawable size (may differ from logical on HiDPI displays).
    SDL_GL_GetDrawableSize(nativeHandle, &cachedWidth, &cachedHeight);
    // Logical window size (what the OS / SDL reports for event coordinates).
    SDL_GetWindowSize(nativeHandle, &logicalWidth_, &logicalHeight_);

    fprintf(stderr, "PlatformWindow: logical=%dx%d  physical=%dx%d  scale=%.2fx%.2f\n",
            logicalWidth_, logicalHeight_,
            cachedWidth,   cachedHeight,
            dpiScaleX(),   dpiScaleY());

    glContext_ = SDL_GL_CreateContext(nativeHandle);
    if (!glContext_) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(nativeHandle); nativeHandle = nullptr;
        SDL_Quit(); return false;
    }
    SDL_GL_MakeCurrent(nativeHandle, glContext_);
    SDL_GL_SetSwapInterval(1);

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        fprintf(stderr, "PlatformWindow: gladLoadGLLoader failed\n");
        SDL_GL_DeleteContext(glContext_); glContext_ = nullptr;
        SDL_DestroyWindow(nativeHandle); nativeHandle = nullptr;
        SDL_Quit(); return false;
    }

    fprintf(stderr, "GL: %s | GLSL: %s | %s\n",
            (const char*)glGetString(GL_VERSION),
            (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION),
            (const char*)glGetString(GL_RENDERER));

    // ── Canvas2DGL (shared context for all CanvasWidgets) ─────────────────────
    canvasGL_ = new Canvas2DGL();
    if (!canvasGL_->init()) {
        fprintf(stderr, "PlatformWindow: Canvas2DGL init failed\n");
        delete canvasGL_; canvasGL_ = nullptr;
        SDL_GL_DeleteContext(glContext_); glContext_ = nullptr;
        SDL_DestroyWindow(nativeHandle); nativeHandle = nullptr;
        SDL_Quit(); return false;
    }

    auto regFont = [this](const char* name, const char* path) {
        if (!Canvas2D::registerFont(canvasGL_, name, path))
            fprintf(stderr, "PlatformWindow: font '%s' not found at '%s'\n", name, path);
    };
    regFont("sans",             "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    regFont("sans-bold",        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf");
    regFont("sans-italic",      "/usr/share/fonts/truetype/dejavu/DejaVuSans-Oblique.ttf");
    regFont("sans-bold-italic", "/usr/share/fonts/truetype/dejavu/DejaVuSans-BoldOblique.ttf");
    regFont("mono",             "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf");
    regFont("mono-bold",        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf");
    // Liberation fallbacks (Fedora / RHEL)
    regFont("sans",      "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf");
    regFont("sans-bold", "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf");

    // ── Cairo compositor ──────────────────────────────────────────────────────
    cairoCompositor_ = new CairoCompositor();
    if (!cairoCompositor_->init())
        fprintf(stderr, "PlatformWindow: CairoCompositor init failed\n");

    // ── Cairo CPU surface — allocated at LOGICAL size.
    // The compositor stretches it over the full NDC quad so it fills the
    // physical viewport correctly without any extra scaling code.
    cairoState = new CairoState();
    if (!cairoState->rebuild(logicalWidth_, logicalHeight_))
        fprintf(stderr, "PlatformWindow: CairoState::rebuild failed\n");

    // Notify any already-registered canvas widgets.
    for (CanvasWidget* cw : canvasWidgets_)
        if (cw) cw->setCanvasGL(canvasGL_);

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

    if (cairoCompositor_) {
        cairoCompositor_->destroy();
        delete cairoCompositor_;
        cairoCompositor_ = nullptr;
    }

    delete cairoState;
    cairoState = nullptr;

    if (canvasGL_) {
        canvasGL_->destroy();
        delete canvasGL_;
        canvasGL_ = nullptr;
    }

    if (glContext_) { SDL_GL_DeleteContext(glContext_); glContext_ = nullptr; }
    if (nativeHandle){ SDL_DestroyWindow(nativeHandle); nativeHandle = nullptr; }
    SDL_Quit();
}


NativeWindow PlatformWindow::handle() const {
    return nativeHandle;
}
bool PlatformWindow::isMouseCaptured() const {
    return mouseCapture;
}

// ============================================================================
// PlatformWindow::run
// ============================================================================
int PlatformWindow::run() {
    SDL_Event e;

    while (running) {

        bool hasEvent = (SDL_WaitEventTimeout(&e, 16) != 0);
        if (hasEvent) handleSDLEvent(e);
        while (SDL_PollEvent(&e)) handleSDLEvent(e);

        if (!dirty) {
            for (CanvasWidget* cw : canvasWidgets_) {
                if (cw && cw->isInitialized() && cw->needsRepaint()) {
                    dirty = true; break;
                }
            }
        }

        fluxProcessHttpEvents();

        if (!dirty) continue;
        dirty = false;

        SDL_GL_MakeCurrent(nativeHandle, glContext_);

        // ── Pass 1: raw GL pre-render (FBO passes, texture uploads) ───────────
        for (CanvasWidget* cw : canvasWidgets_)
            if (cw && cw->isInitialized()) cw->preRenderPass();

        // ── Clear window using the PHYSICAL drawable size ─────────────────────
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, cachedWidth, cachedHeight);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        glEnable(GL_MULTISAMPLE);

        // ── Pass 2: canvas widgets (Canvas2D + scrollbars) ────────────────────
        for (CanvasWidget* cw : canvasWidgets_)
            if (cw && cw->isInitialized()) cw->glRenderPass();

        // ── Pass 3: Cairo UI overlay ──────────────────────────────────────────
        // Paint all UI widgets into Cairo at LOGICAL resolution, then
        // composite the texture over the full physical viewport via NDC quad.
        if (cairoState && cairoState->cr) {
            cairo_save(cairoState->cr);
            cairo_set_operator(cairoState->cr, CAIRO_OPERATOR_CLEAR);
            cairo_paint(cairoState->cr);
            cairo_restore(cairoState->cr);

            GraphicsContext ctx(cairoState->cr, logicalWidth_, logicalHeight_);
            if (callbacks.onPaint)
                callbacks.onPaint(ctx, logicalWidth_, logicalHeight_);

            cairo_surface_flush(cairoState->cairoSurf);

            if (cairoCompositor_) {
                // Restore full physical viewport for the composite blit.
                glViewport(0, 0, cachedWidth, cachedHeight);
                cairoCompositor_->draw(
                    cairoState->pixels.data(),
                    logicalWidth_, logicalHeight_);
            }
        }

        SDL_GL_SwapWindow(nativeHandle);
    }

    return 0;
}

// ============================================================================
// Canvas registry
// ============================================================================
void PlatformWindow::registerCanvas(CanvasWidget* c) {
    if (!c) return;
    canvasWidgets_.push_back(c);
    if (canvasGL_) c->setCanvasGL(canvasGL_);
}

void PlatformWindow::unregisterCanvas(CanvasWidget* c) {
    if (capturedCanvas_ == c) capturedCanvas_ = nullptr;
    if (focusedCanvas_  == c) focusedCanvas_  = nullptr;
    canvasWidgets_.erase(
        std::remove(canvasWidgets_.begin(), canvasWidgets_.end(), c),
        canvasWidgets_.end());
}

CanvasWidget* PlatformWindow::hitTestCanvas(int sx, int sy) {
    // sx/sy are LOGICAL pixel coordinates from SDL events.
    // CanvasWidget::containsPoint also works in logical pixels — correct.
    for (auto it = canvasWidgets_.rbegin(); it != canvasWidgets_.rend(); ++it) {
        CanvasWidget* cw = *it;
        if (cw && cw->isInitialized() && cw->containsPoint(sx, sy)) return cw;
    }
    return nullptr;
}

// ============================================================================
// handleSDLEvent
// All SDL mouse/key event coordinates are in LOGICAL pixels.
// ============================================================================
void PlatformWindow::handleSDLEvent(const SDL_Event& e) {

    if (e.type == gFluxHttpEventType) return;

    if (e.type == CanvasWidget::repaintEventType()) {
        dirty = true; return;
    }

    switch (e.type) {

    case SDL_QUIT:
        running = false;
        break;

    case SDL_MOUSEBUTTONDOWN: {
        CanvasWidget* cw = hitTestCanvas(e.button.x, e.button.y);
        if (cw) {
            capturedCanvas_ = cw;
            focusedCanvas_  = cw;
            SDL_Event local = e;
            // Deliver event in widget-local logical coordinates.
            local.button.x -= cw->x;
            local.button.y -= cw->y;
            cw->handleSDLEvent(local);
            dirty = true; break;
        }
        focusedCanvas_ = nullptr;
        if (e.button.button == SDL_BUTTON_LEFT && callbacks.onMouseDown)
            if (callbacks.onMouseDown(e.button.x, e.button.y)) dirty = true;
        if (e.button.button == SDL_BUTTON_RIGHT && callbacks.onRightClick)
            if (callbacks.onRightClick(e.button.x, e.button.y)) dirty = true;
        break;
    }

    case SDL_MOUSEBUTTONUP: {
        CanvasWidget* cw = capturedCanvas_
                           ? capturedCanvas_
                           : hitTestCanvas(e.button.x, e.button.y);
        if (e.button.button == SDL_BUTTON_LEFT ||
            e.button.button == SDL_BUTTON_MIDDLE)
            capturedCanvas_ = nullptr;
        if (cw) {
            SDL_Event local = e;
            local.button.x -= cw->x;
            local.button.y -= cw->y;
            cw->handleSDLEvent(local);
            dirty = true; break;
        }
        if (e.button.button == SDL_BUTTON_LEFT && callbacks.onMouseUp)
            if (callbacks.onMouseUp(e.button.x, e.button.y)) dirty = true;
        break;
    }

    case SDL_MOUSEMOTION: {
        CanvasWidget* cw = capturedCanvas_
                           ? capturedCanvas_
                           : hitTestCanvas(e.motion.x, e.motion.y);
        if (cw) {
            SDL_Event local = e;
            local.motion.x -= cw->x;
            local.motion.y -= cw->y;
            cw->handleSDLEvent(local);
            dirty = true; break;
        }
        if (callbacks.onMouseMove)
            if (callbacks.onMouseMove(e.motion.x, e.motion.y)) dirty = true;
        break;
    }

    case SDL_MOUSEWHEEL: {
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        CanvasWidget* cw = hitTestCanvas(mx, my);
        if (cw) { cw->handleSDLEvent(e); dirty = true; break; }
        int delta = e.wheel.y * WHEEL_DELTA;
        if (callbacks.onMouseWheel && callbacks.onMouseWheel(delta)) dirty = true;
        break;
    }

    case SDL_KEYDOWN:
        if (focusedCanvas_) {
            focusedCanvas_->handleSDLEvent(e); dirty = true; break;
        }
        if (callbacks.onKeyDown && callbacks.onKeyDown(e.key.keysym.sym))
            dirty = true;
        break;

    case SDL_KEYUP:
        if (focusedCanvas_) { focusedCanvas_->handleSDLEvent(e); break; }
        break;

    case SDL_TEXTINPUT:
        if (callbacks.onChar && e.text.text[0] != '\0') {
            unsigned char b0 = static_cast<unsigned char>(e.text.text[0]);
            wchar_t ch = 0;
            if      (b0 < 0x80)
                ch = static_cast<wchar_t>(b0);
            else if ((b0 & 0xE0) == 0xC0 && e.text.text[1])
                ch = static_cast<wchar_t>(
                    ((b0 & 0x1F) << 6) |
                    (static_cast<unsigned char>(e.text.text[1]) & 0x3F));
            else if ((b0 & 0xF0) == 0xE0 && e.text.text[1] && e.text.text[2])
                ch = static_cast<wchar_t>(
                    ((b0 & 0x0F) << 12) |
                    ((static_cast<unsigned char>(e.text.text[1]) & 0x3F) << 6) |
                    (static_cast<unsigned char>(e.text.text[2]) & 0x3F));
            if (ch && callbacks.onChar(ch)) dirty = true;
        }
        break;

    case SDL_WINDOWEVENT:
        switch (e.window.event) {

        case SDL_WINDOWEVENT_RESIZED:
        case SDL_WINDOWEVENT_SIZE_CHANGED: {
            // Always re-query both physical and logical sizes on resize.
            SDL_GL_GetDrawableSize(nativeHandle, &cachedWidth, &cachedHeight);
            SDL_GetWindowSize(nativeHandle, &logicalWidth_, &logicalHeight_);

            fprintf(stderr, "PlatformWindow resize: logical=%dx%d  physical=%dx%d\n",
                    logicalWidth_, logicalHeight_, cachedWidth, cachedHeight);

            // Rebuild Cairo surface at the new LOGICAL size.
            if (cairoState) cairoState->rebuild(logicalWidth_, logicalHeight_);

            // Invalidate cached texture size in compositor.
            if (cairoCompositor_) {
                cairoCompositor_->texW = 0;
                cairoCompositor_->texH = 0;
            }

            if (callbacks.onResize && cairoState && cairoState->cr) {
                GraphicsContext ctx(cairoState->cr, logicalWidth_, logicalHeight_);
                callbacks.onResize(ctx, logicalWidth_, logicalHeight_);
            }

            // Notify canvas widgets — they will recompute their physical size
            // from their logical width/height + the new DPI scale internally.
            for (CanvasWidget* cw : canvasWidgets_)
                if (cw) cw->onWindowResize(cachedWidth, cachedHeight);

            dirty = true; break;
        }

        case SDL_WINDOWEVENT_EXPOSED:
            dirty = true; break;

        case SDL_WINDOWEVENT_LEAVE:
            capturedCanvas_ = nullptr;
            for (CanvasWidget* cw : canvasWidgets_)
                if (cw) cw->onMouseLeave();
            if (callbacks.onMouseLeave) callbacks.onMouseLeave();
            break;

        case SDL_WINDOWEVENT_FOCUS_LOST:
            capturedCanvas_ = nullptr;
            focusedCanvas_  = nullptr;
            if (callbacks.onFocusLost) callbacks.onFocusLost();
            dirty = true; break;

        case SDL_WINDOWEVENT_FOCUS_GAINED:
            dirty = true; break;
        }
        break;

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
    SDL_Event e; SDL_zero(e);
    Uint32 t    = CanvasWidget::repaintEventType();
    e.type      = (t != 0) ? t : SDL_USEREVENT;
    e.user.code = -1;
    SDL_PushEvent(&e);
}

void PlatformWindow::invalidateRect(int, int, int, int) {
    if (!dirty) { dirty = true; invalidate(); }
}

// ============================================================================
// setTimer / killTimer
// ============================================================================
void PlatformWindow::setTimer(TimerID id, int ms) {
    killTimer(id);
    SDL_TimerID sdlId = SDL_AddTimer(
        static_cast<Uint32>(ms), sdlTimerCallback,
        reinterpret_cast<void*>(static_cast<uintptr_t>(id)));
    if (sdlId != 0) sdlTimerMap[id] = sdlId;
}
void PlatformWindow::killTimer(TimerID id) {
    auto it = sdlTimerMap.find(id);
    if (it != sdlTimerMap.end()) {
        SDL_RemoveTimer(it->second);
        sdlTimerMap.erase(it);
    }
}

bool PlatformWindow::valid() const {
     return nativeHandle != nullptr;
}


// ============================================================================
// Clipboard / input / coords / cursors
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
    SDL_CaptureMouse(SDL_TRUE); mouseCapture = true;
}
void PlatformWindow::releaseMouseInput() {
    SDL_CaptureMouse(SDL_FALSE); mouseCapture = false;
}
PlatformWindow::ScreenPoint PlatformWindow::clientToScreen(int cx, int cy) const {
    int wx = 0, wy = 0;
    if (nativeHandle) SDL_GetWindowPosition(nativeHandle, &wx, &wy);
    return {wx + cx, wy + cy};
}
PlatformWindow::ScreenPoint PlatformWindow::screenToClient(int sx, int sy) const {
    int wx = 0, wy = 0;
    if (nativeHandle) SDL_GetWindowPosition(nativeHandle, &wx, &wy);
    return {sx - wx, sy - wy};
}
PlatformWindow::ClientSize PlatformWindow::getClientSize() const {
    return {logicalWidth_, logicalHeight_};
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
void PlatformWindow::startupGdiplus() {}
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