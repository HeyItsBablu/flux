// flux_canvas_linux.cpp
#if defined(__linux__) && !defined(__ANDROID__)

#include "flux/flux_canvas.hpp"
#include <cairo/cairo.h>
#include <SDL2/SDL.h>
#include <cassert>

void Canvas2D_setCairo(Canvas2D &ctx, cairo_t *cr);

// ─────────────────────────────────────────────────────────────────────────────
// Linux-only per-widget state
// ─────────────────────────────────────────────────────────────────────────────

struct LinuxCanvasState
{
    bool initialized = false;
    bool repaintPending = false;
    int lastW = 0; // physical pixels
    int lastH = 0;
    int physX = 0;
    int physY = 0;
    cairo_t *cr = nullptr;
};

#include <unordered_map>
static std::unordered_map<CanvasWidget *, LinuxCanvasState> &getLinuxStateMap()
{
    static std::unordered_map<CanvasWidget *, LinuxCanvasState> s_map;
    return s_map;
}

static LinuxCanvasState &linuxState(CanvasWidget *w)
{
    return getLinuxStateMap()[w];
}

// ─────────────────────────────────────────────────────────────────────────────
// SDL repaint event type
// ─────────────────────────────────────────────────────────────────────────────

static Uint32 s_repaintEventType_ = 0;

void CanvasWidget::initEventType()
{
    if (s_repaintEventType_ == 0)
    {
        Uint32 t = SDL_RegisterEvents(1);
        s_repaintEventType_ = (t != static_cast<Uint32>(-1)) ? t : static_cast<Uint32>(SDL_USEREVENT);
    }
}
Uint32 CanvasWidget::repaintEventType() { return s_repaintEventType_; }

// ─────────────────────────────────────────────────────────────────────────────
// DPI helpers
// ─────────────────────────────────────────────────────────────────────────────

static float getDpiScaleX()
{
    auto *inst = FluxUI::getCurrentInstance();
    if (!inst)
        return 1.f;
    auto *pw = inst->getPlatformWindowPtr();
    return pw ? pw->dpiScaleX() : 1.f;
}
static float getDpiScaleY()
{
    auto *inst = FluxUI::getCurrentInstance();
    if (!inst)
        return 1.f;
    auto *pw = inst->getPlatformWindowPtr();
    return pw ? pw->dpiScaleY() : 1.f;
}
static int getWindowPhysW()
{
    auto *inst = FluxUI::getCurrentInstance();
    if (!inst)
        return 1;
    auto *pw = inst->getPlatformWindowPtr();
    return pw ? pw->clientWidth() : 1;
}
static int getWindowPhysH()
{
    auto *inst = FluxUI::getCurrentInstance();
    if (!inst)
        return 1;
    auto *pw = inst->getPlatformWindowPtr();
    return pw ? pw->clientHeight() : 1;
}

static int toPhysX(int lx) { return int(float(lx) * getDpiScaleX()); }
static int toPhysY(int ly) { return int(float(ly) * getDpiScaleY()); }
static int toPhysW(int lw) { return int(float(lw) * getDpiScaleX()); }
static int toPhysH(int lh) { return int(float(lh) * getDpiScaleY()); }

// ─────────────────────────────────────────────────────────────────────────────
// Platform registration helpers
// ─────────────────────────────────────────────────────────────────────────────

static void registerWithPlatform(CanvasWidget *w)
{
    auto *inst = FluxUI::getCurrentInstance();
    if (!inst)
        return;
    auto *pw = inst->getPlatformWindowPtr();
    if (pw)
        pw->registerCanvas_public(w);
}
static void unregisterWithPlatform(CanvasWidget *w)
{
    auto *inst = FluxUI::getCurrentInstance();
    if (!inst)
        return;
    auto *pw = inst->getPlatformWindowPtr();
    if (pw)
        pw->unregisterCanvas_public(w);
}

// ─────────────────────────────────────────────────────────────────────────────
// scheduleRepaint
// ─────────────────────────────────────────────────────────────────────────────

static void scheduleRepaint(CanvasWidget *w)
{
    if (w->onViewportChanged)
        w->onViewportChanged(w->vp_.zoom());
    auto &s = linuxState(w);
    if (!s.repaintPending)
    {
        s.repaintPending = true;
        SDL_Event e;
        SDL_zero(e);
        e.type = (s_repaintEventType_ != 0u) ? s_repaintEventType_ : static_cast<Uint32>(SDL_USEREVENT);
        e.user.code = 0;
        e.user.data1 = w;
        SDL_PushEvent(&e);
    }
}

static void syncPhysicalGeometry(CanvasWidget *w)
{
    auto &s = linuxState(w);

    const int newPhysW = toPhysW(w->width);
    const int newPhysH = toPhysH(w->height);
    const int newPhysX = toPhysX(w->x);
    const int newPhysY = toPhysY(w->y);

    // Always update position — widget may move without resizing.
    s.physX = newPhysX;
    s.physY = newPhysY;

    const bool sizeChanged = (newPhysW != s.lastW || newPhysH != s.lastH);
    if (!sizeChanged || newPhysW <= 0 || newPhysH <= 0)
        return;

    s.lastW = newPhysW;
    s.lastH = newPhysH;

    w->canvasW_ = newPhysW;
    w->canvasH_ = newPhysH;
    w->vp_.setCanvasSize(newPhysW, newPhysH);

    w->updateViewportSize(s.lastW, s.lastH);
    w->updateSBGeometry(s.lastW, s.lastH);

    if (w->activeSurface_)
        w->activeSurface_->resize(newPhysW, newPhysH);
    if (w->onGLResize)
        w->onGLResize(s.lastW, s.lastH);

    // Fit content to new size.
    w->vp_.fitToView();
}

// ─────────────────────────────────────────────────────────────────────────────
// initCanvas  — one-time GL setup; size may still be default here
// ─────────────────────────────────────────────────────────────────────────────

static void initCanvas(CanvasWidget *w)
{
    auto &s = linuxState(w);
    assert(!s.initialized);

    s.lastW = (w->width > 0) ? toPhysW(w->width) : 1;
    s.lastH = (w->height > 0) ? toPhysH(w->height) : 1;
    s.physX = toPhysX(w->x);
    s.physY = toPhysY(w->y);

    // Initialise canvasW_/canvasH_ to physical size right away.
    w->canvasW_ = s.lastW;
    w->canvasH_ = s.lastH;

    w->vp_.init(s.lastW, s.lastH, w->canvasW_, w->canvasH_);
    w->updateViewportSize(s.lastW, s.lastH);
    w->updateSBGeometry(s.lastW, s.lastH);
    w->vp_.fitToView();

    w->activatePendingSurface();
    w->lastTick_ = CanvasWidget::Clock::now();
    w->frameDt_ = 0.0;

    s.initialized = true;

    if (w->onViewportChanged)
        w->onViewportChanged(w->vp_.zoom());
    if (w->onGLResize)
        w->onGLResize(s.lastW, s.lastH);
}

// ─────────────────────────────────────────────────────────────────────────────
// CanvasWidget member implementations (Linux)
// ─────────────────────────────────────────────────────────────────────────────

CanvasWidget::CanvasWidget()
    : hBar_(CustomScrollbar::Axis::Horizontal), vBar_(CustomScrollbar::Axis::Vertical)
{
    autoWidth = true;
    autoHeight = true;
    width = 400;
    height = 300;
    registerWithPlatform(this);
}

CanvasWidget::~CanvasWidget()
{
    if (activeSurface_)
    {
        activeSurface_->destroy();
        activeSurface_.reset();
    }
    pendingSurface_.reset();
    linuxState(this).initialized = false;
    getLinuxStateMap().erase(this);
    unregisterWithPlatform(this);
}

std::shared_ptr<CanvasWidget> CanvasWidget::setViewportEnabled(bool e)
{
    viewportEnabled_ = e;
    return ptr();
}
std::shared_ptr<CanvasWidget> CanvasWidget::setScrollbarsEnabled(bool e)
{
    scrollbarsEnabled_ = e;
    auto &s = linuxState(this);
    if (s.initialized)
    {
        updateViewportSize(s.lastW, s.lastH);
        scheduleRepaint(this);
    }
    return ptr();
}
bool CanvasWidget::scrollbarsEnabled() const { return scrollbarsEnabled_; }

RenderSurface *CanvasWidget::getSurface() const { return activeSurface_.get(); }
const Viewport &CanvasWidget::viewport() const { return vp_; }
Viewport &CanvasWidget::viewport() { return vp_; }

std::shared_ptr<CanvasWidget> CanvasWidget::setSize(int w, int h)
{
    width = w;
    height = h;
    autoWidth = false;
    autoHeight = false;
    markNeedsLayout();
    return ptr();
}

std::shared_ptr<CanvasWidget> CanvasWidget::setCanvasSize(int w, int h)
{

    canvasW_ = toPhysW(w);
    canvasH_ = toPhysH(h);
    auto &s = linuxState(this);
    if (s.initialized)
    {
        vp_.setCanvasSize(canvasW_, canvasH_);
        if (activeSurface_)
            activeSurface_->resize(canvasW_, canvasH_);
    }
    return ptr();
}

std::shared_ptr<CanvasWidget> CanvasWidget::redraw()
{
    markNeedsPaint();
    return ptr();
}

void CanvasWidget::computeLayout(GraphicsContext & /*ctx*/,
                                 const BoxConstraints &c, FontCache &)
{
    if (autoWidth)
        width = (c.maxWidth < kUnbounded) ? c.maxWidth : width;
    if (autoHeight)
        height = (c.maxHeight < kUnbounded) ? c.maxHeight : height;

    needsLayout = false;
    // Trigger a preRenderPass so syncPhysicalGeometry picks up the new size.
    scheduleRepaint(this);
}

void CanvasWidget::render(GraphicsContext &ctx, FontCache &)
{
    // Store the cairo_t so glRenderPass can use it.
    if (ctx.cr)
        setCairo(ctx.cr);
    needsPaint = false;
}

void CanvasWidget::onDetach()
{
    if (activeSurface_)
    {
        activeSurface_->destroy();
        activeSurface_.reset();
    }
    pendingSurface_.reset();
    getLinuxStateMap().erase(this);
    unregisterWithPlatform(this);
    Widget::onDetach();
}

void CanvasWidget::markNeedsPaint()
{
    Widget::markNeedsPaint();
    scheduleRepaint(this);
}

void CanvasWidget::setCairo(cairo_t *cr)
{
    linuxState(this).cr = cr;
    auto &s = linuxState(this);
    if (cr && !s.initialized)
        initCanvas(this);
}

void CanvasWidget::onWindowResize(int /*newW*/, int /*newH*/)
{
    auto &s = linuxState(this);
    if (!s.initialized)
        return;
    // Force full recalc on the next preRenderPass.
    s.lastW = 0;
    s.lastH = 0;
    scheduleRepaint(this);
}

void CanvasWidget::preRenderPass()
{
    auto &s = linuxState(this);

    if (!s.cr || !s.initialized)
        return;

    // Sync logical layout → physical GL dims every frame.
    syncPhysicalGeometry(this);

    if (pendingSurface_)
        activatePendingSurface();
    if (!activeSurface_)
        return;

    auto now = Clock::now();
    frameDt_ = std::chrono::duration<double>(now - lastTick_).count();
    lastTick_ = now;

    activeSurface_->update(frameDt_);
    activeSurface_->preRender();
}

void CanvasWidget::glRenderPass()
{
    auto &s = linuxState(this);
    if (!s.cr || !s.initialized || !activeSurface_)
        return;

    s.repaintPending = false;

    cairo_t *cr = s.cr;

    // Clip to widget logical bounds.
    cairo_save(cr);
    cairo_rectangle(cr, (double)x, (double)y,
                    (double)width, (double)height);
    cairo_clip(cr);

    // Apply viewport transform (pan + zoom) relative to widget origin.
    cairo_translate(cr, (double)x, (double)y);
    float z = vp_.zoom();
    float ox = -vp_.offsetX() * z;
    float oy = -vp_.offsetY() * z;
    cairo_translate(cr, ox, oy);
    cairo_scale(cr, z, z);

    // Hand off to the surface.
    {
        Canvas2D ctx(backend_, canvasW_, canvasH_);
        Canvas2D_setCairo(ctx, cr);
        activeSurface_->render(ctx);
    }

    cairo_restore(cr);

    if (scrollbarsEnabled_)
    {
        auto &s = linuxState(this);
        updateSBGeometry(s.lastW, s.lastH);
        hBar_.tick(frameDt_);
        vBar_.tick(frameDt_);

        GraphicsContext gctx(cr, width, height);
        Painter p(gctx);
        p.drawScrollbar(hBar_, s.lastW, s.lastH);
        p.drawScrollbar(vBar_, s.lastW, s.lastH);

        if ((hBar_.needsRedraw() || vBar_.needsRedraw()) &&
            !linuxState(this).repaintPending)
            scheduleRepaint(this);
    }

    if (activeSurface_->needsContinuousRedraw())
        scheduleRepaint(this);
}

bool CanvasWidget::isInitialized() const { return linuxState(const_cast<CanvasWidget *>(this)).initialized; }
bool CanvasWidget::needsRepaint() const { return linuxState(const_cast<CanvasWidget *>(this)).repaintPending; }

bool CanvasWidget::containsPoint(int wx, int wy) const
{
    return wx >= x && wx < (x + width) &&
           wy >= y && wy < (y + height);
}

void CanvasWidget::onMouseLeave()
{
    hBar_.onMouseLeave();
    vBar_.onMouseLeave();
    scheduleRepaint(this);
}

// ── SDL event forwarding ──────────────────────────────────────────────────────
void CanvasWidget::handleSDLEvent(const SDL_Event &e)
{
    switch (e.type)
    {
    case SDL_MOUSEBUTTONDOWN:
        onMouseButtonDown(e.button);
        break;
    case SDL_MOUSEBUTTONUP:
        onMouseButtonUp(e.button);
        break;
    case SDL_MOUSEMOTION:
        onMouseMotion(e.motion);
        break;
    case SDL_MOUSEWHEEL:
        onMouseWheel(e.wheel);
        break;
    case SDL_KEYDOWN:
        onKeyDownEvent(e.key);
        break;
    case SDL_KEYUP:
        onKeyUpEvent(e.key);
        break;
    default:
        break;
    }
}

void CanvasWidget::onMouseButtonDown(const SDL_MouseButtonEvent &e)
{
    SDL_CaptureMouse(SDL_TRUE);
    int sx = e.x, sy = e.y;
    if (e.button == SDL_BUTTON_MIDDLE && viewportEnabled_)
    {
        beginPan(sx, sy);
        return;
    }
    if (e.button == SDL_BUTTON_LEFT)
    {
        bool hC = hBar_.onMouseDown(sx, sy,
                                    [this](float t)
                                    { applyHScrollFraction(t); });
        bool vC = !hC && vBar_.onMouseDown(sx, sy,
                                           [this](float t)
                                           { applyVScrollFraction(t); });
        if (!hC && !vC)
        {
            if (viewportEnabled_ &&
                (SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_SPACE] != 0))
            {
                beginPan(sx, sy);
            }
            else if (activeSurface_)
            {
                auto [cx, cy] = vp_.screenToCanvas(float(sx), float(sy));
                activeSurface_->onMouseDown(cx, cy);
            }
        }
        scheduleRepaint(this);
    }
    if (e.button == SDL_BUTTON_RIGHT && activeSurface_)
    {
        auto [cx, cy] = vp_.screenToCanvas(float(sx), float(sy));
        activeSurface_->onRightMouseDown(cx, cy);
    }
}

void CanvasWidget::onMouseButtonUp(const SDL_MouseButtonEvent &e)
{
    SDL_CaptureMouse(SDL_FALSE);
    int sx = e.x, sy = e.y;
    if (e.button == SDL_BUTTON_MIDDLE)
    {
        panning_ = false;
        return;
    }
    if (e.button == SDL_BUTTON_LEFT)
    {
        bool hR = hBar_.onMouseUp(sx, sy);
        bool vR = vBar_.onMouseUp(sx, sy);
        if (!hR && !vR)
        {
            if (panning_)
            {
                panning_ = false;
            }
            else if (activeSurface_)
            {
                auto [cx, cy] = vp_.screenToCanvas(float(sx), float(sy));
                activeSurface_->onMouseUp(cx, cy);
            }
        }
        scheduleRepaint(this);
    }
    if (e.button == SDL_BUTTON_RIGHT && activeSurface_)
    {
        auto [cx, cy] = vp_.screenToCanvas(float(e.x), float(e.y));
        activeSurface_->onMouseUp(cx, cy);
    }
}

void CanvasWidget::onMouseMotion(const SDL_MouseMotionEvent &e)
{
    int sx = e.x, sy = e.y;
    bool hDrag = hBar_.onMouseMove(sx, sy);
    bool vDrag = vBar_.onMouseMove(sx, sy);
    scheduleRepaint(this);
    if (hDrag || vDrag)
        return;
    if (panning_)
    {
        continuePan(sx, sy);
    }
    else if (activeSurface_)
    {
        auto [cx, cy] = vp_.screenToCanvas(float(sx), float(sy));
        activeSurface_->onMouseMove(cx, cy);
    }
}

void CanvasWidget::onMouseWheel(const SDL_MouseWheelEvent &e)
{
    if (!viewportEnabled_)
        return;
    static constexpr int kWheelDelta = 120;
    int delta = e.y * kWheelDelta;
    bool ctrl = platformCtrlDown();
    bool shift = platformShiftDown();
    int wmx, wmy;
    SDL_GetMouseState(&wmx, &wmy);
    int mx = wmx - x, my = wmy - y;
    if (ctrl)
    {
        float f = (delta > 0) ? 1.1f : 1.f / 1.1f;
        vp_.zoomToward(float(mx), float(my), f);
    }
    else if (shift)
    {
        vp_.panByScreen(float(delta) * 0.5f, 0.f);
    }
    else
    {
        vp_.panByScreen(0.f, -(float(delta) / float(kWheelDelta)) * 40.f);
    }
    pokeScrollbars();
    scheduleRepaint(this);
}

void CanvasWidget::onKeyDownEvent(const SDL_KeyboardEvent &e)
{
    bool ctrl = platformCtrlDown();
    bool shift = platformShiftDown();
    bool alt = false;
    bool consumed = false;
    if (ctrl && viewportEnabled_)
    {
        SDL_Keycode k = e.keysym.sym;
        if (k == SDLK_EQUALS || k == SDLK_PLUS || k == SDLK_KP_PLUS)
        {
            vp_.zoomIn();
            pokeScrollbars();
            scheduleRepaint(this);
            consumed = true;
        }
        else if (k == SDLK_MINUS || k == SDLK_KP_MINUS)
        {
            vp_.zoomOut();
            pokeScrollbars();
            scheduleRepaint(this);
            consumed = true;
        }
        else if (k == SDLK_0 || k == SDLK_KP_0)
        {
            vp_.resetZoom();
            pokeScrollbars();
            scheduleRepaint(this);
            consumed = true;
        }
    }
    if (!consumed && activeSurface_)
    {
        KeyEvent ke{0, e.keysym.sym, ctrl, shift, alt};
        activeSurface_->onKeyDown(ke);
    }
}

void CanvasWidget::onKeyUpEvent(const SDL_KeyboardEvent &e)
{
    if (activeSurface_)
    {
        KeyEvent ke{0, e.keysym.sym, false, false, false};
        activeSurface_->onKeyUp(ke);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Shared helpers  (glW / glH are PHYSICAL pixels)
// ─────────────────────────────────────────────────────────────────────────────

void CanvasWidget::viewportDims(int glW, int glH, int &vpW, int &vpH) const
{
    if (!viewportEnabled_ || !scrollbarsEnabled_)
    {
        vpW = glW;
        vpH = glH;
        return;
    }
    ScrollbarInfo h = vp_.scrollbarH(), v = vp_.scrollbarV();
    int sbThickX = int(kSBThick * getDpiScaleX());
    int sbThickY = int(kSBThick * getDpiScaleY());
    vpW = glW - (v.visible ? sbThickX : 0);
    vpH = glH - (h.visible ? sbThickY : 0);
    if (vpW < 1)
        vpW = 1;
    if (vpH < 1)
        vpH = 1;
}
void CanvasWidget::updateViewportSize(int glW, int glH)
{
    int vpW, vpH;
    viewportDims(glW, glH, vpW, vpH);
    vp_.setViewSize(vpW, vpH);
}
void CanvasWidget::updateSBGeometry(int glW, int glH)
{
    float sbThickX = kSBThick * getDpiScaleX();
    float sbThickY = kSBThick * getDpiScaleY();
    ScrollbarInfo h = vp_.scrollbarH(), v = vp_.scrollbarV();
    float hLen = float(glW) - (v.visible ? sbThickX : 0.f);
    hBar_.setGeometry(0.f, float(glH) - sbThickY, hLen);
    hBar_.setThumb(h.thumbMin, h.thumbMax,
                   h.visible && scrollbarsEnabled_ && viewportEnabled_);
    float vLen = float(glH) - (h.visible ? sbThickY : 0.f);
    vBar_.setGeometry(float(glW) - sbThickX, 0.f, vLen);
    vBar_.setThumb(v.thumbMin, v.thumbMax,
                   v.visible && scrollbarsEnabled_ && viewportEnabled_);
}
void CanvasWidget::beginPan(int sx, int sy)
{
    panning_ = true;
    panStartSX_ = sx;
    panStartSY_ = sy;
    panStartOX_ = vp_.offsetX();
    panStartOY_ = vp_.offsetY();
}
void CanvasWidget::continuePan(int sx, int sy)
{
    float dx = float(sx - panStartSX_), dy = float(sy - panStartSY_);
    vp_.setOffset(panStartOX_ - dx / vp_.zoom(), panStartOY_ + dy / vp_.zoom());
    pokeScrollbars();
    scheduleRepaint(this);
}
void CanvasWidget::pokeScrollbars()
{
    hBar_.poke();
    vBar_.poke();
}
void CanvasWidget::applyHScrollFraction(float thumbMin)
{
    float span = vp_.scrollbarH().thumbMax - vp_.scrollbarH().thumbMin;
    float range = vp_.canvasW() - vp_.viewW() / vp_.zoom();
    if (range <= 0.f)
        return;
    vp_.setOffsetX(thumbMin / (1.f - span) * range);
    pokeScrollbars();
    scheduleRepaint(this);
}
void CanvasWidget::applyVScrollFraction(float thumbMin)
{
    float span = vp_.scrollbarV().thumbMax - vp_.scrollbarV().thumbMin;
    float range = vp_.canvasH() - vp_.viewH() / vp_.zoom();
    if (range <= 0.f)
        return;
    float t = 1.f - thumbMin - span;
    vp_.setOffsetY(t / (1.f - span) * range);
    pokeScrollbars();
    scheduleRepaint(this);
}
void CanvasWidget::activatePendingSurface()
{
    if (!pendingSurface_)
        return;
    if (activeSurface_)
    {
        activeSurface_->destroy();
        activeSurface_.reset();
    }
    activeSurface_ = pendingSurface_;
    pendingSurface_.reset();
    activeSurface_->initialize(canvasW_, canvasH_);
    vp_.setCanvasSize(canvasW_, canvasH_);
}

void CanvasWidget::setBackend(Canvas2DBackend *b)
{
    backend_ = b;
}
#endif