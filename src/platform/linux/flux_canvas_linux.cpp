// flux_canvas_linux.cpp
#if defined(__linux__) && !defined(__ANDROID__)

#include "flux/flux_canvas.hpp"
#include <cairo/cairo.h>
#include <SDL2/SDL.h>
#include <cassert>
#include <unordered_map>

void Canvas2D_setCairo(Canvas2D &ctx, cairo_t *cr);
extern Canvas2DBackend *Canvas2DBackend_create();
extern void Canvas2DBackend_destroy(Canvas2DBackend *);

// ─────────────────────────────────────────────────────────────────────────────
// Linux-only per-widget state
//
// Each CanvasWidget owns its own Canvas2DBackend (created lazily on first
// render, destroyed in the widget's own destructor) — mirroring exactly how
// flux_canvas_win32.cpp's Win32D2DCanvasState owns its own Canvas2DD2D/
// Canvas2DBackend per widget rather than sharing one from PlatformWindow.
// ─────────────────────────────────────────────────────────────────────────────

struct LinuxCanvasState
{
    bool initialized = false;
    bool repaintPending = false;
    int lastW = 0; // physical pixels
    int lastH = 0;
    int physX = 0;
    int physY = 0;
    Canvas2DBackend *backend = nullptr; // owned by this widget
};

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

static int toPhysX(int lx) { return int(float(lx) * getDpiScaleX()); }
static int toPhysY(int ly) { return int(float(ly) * getDpiScaleY()); }
static int toPhysW(int lw) { return int(float(lw) * getDpiScaleX()); }
static int toPhysH(int lh) { return int(float(lh) * getDpiScaleY()); }

// ─────────────────────────────────────────────────────────────────────────────
// scheduleRepaint — goes through the generic PlatformWindow::invalidate(),
// not a canvas-specific event type. PlatformWindow has no idea this call
// originated from a CanvasWidget.
// ─────────────────────────────────────────────────────────────────────────────

static void scheduleRepaint(CanvasWidget *w)
{
    if (w->onViewportChanged)
        w->onViewportChanged(w->vp_.zoom());
    auto &s = linuxState(w);
    if (!s.repaintPending)
    {
        s.repaintPending = true;
        if (auto *inst = FluxUI::getCurrentInstance())
            if (auto *pw = inst->getPlatformWindowPtr())
                pw->invalidate();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// initCanvas — creates and owns this widget's own Canvas2DBackend, lazily,
// on first render. No PlatformWindow involvement at all. Mirrors
// flux_canvas_win32.cpp's ensureD2D(): same five logical font slots,
// registered freshly per widget (cheap — Canvas2DBackend on Linux is just a
// borrowed PangoFontMap singleton pointer plus a small name->path map).
// ─────────────────────────────────────────────────────────────────────────────

static void initCanvas(CanvasWidget *w)
{
    auto &s = linuxState(w);
    if (s.initialized)
        return;

    s.backend = Canvas2DBackend_create();
    if (!s.backend)
        return;

    auto reg = [&](const char *name, const char *path)
    {
        Canvas2D::registerFont(s.backend, name, path);
    };
    reg("sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    reg("sans-bold", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf");
    reg("sans-italic", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Oblique.ttf");
    reg("mono", "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf");
    reg("mono-bold", "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf");

    w->backend_ = s.backend;

    s.lastW = (w->width > 0) ? toPhysW(w->width) : 1;
    s.lastH = (w->height > 0) ? toPhysH(w->height) : 1;
    s.physX = toPhysX(w->x);
    s.physY = toPhysY(w->y);

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
// destroyCanvas — tears down this widget's own backend. Called from the
// destructor and onDetach(), mirroring flux_canvas_win32.cpp's destroyD2D().
// ─────────────────────────────────────────────────────────────────────────────

static void destroyCanvas(CanvasWidget *w)
{
    auto it = getLinuxStateMap().find(w);
    if (it == getLinuxStateMap().end())
        return;
    auto &s = it->second;

    if (w->activeSurface_)
    {
        w->activeSurface_->destroy();
        w->activeSurface_.reset();
    }
    w->pendingSurface_.reset();

    if (s.backend)
    {
        Canvas2DBackend_destroy(s.backend);
        s.backend = nullptr;
    }
    w->backend_ = nullptr;

    getLinuxStateMap().erase(it);
}

// ─────────────────────────────────────────────────────────────────────────────
// syncPhysicalGeometry — re-derive physical size/position from the widget's
// own (already-laid-out) logical x/y/width/height. Called every render().
// ─────────────────────────────────────────────────────────────────────────────

static void syncPhysicalGeometry(CanvasWidget *w)
{
    auto &s = linuxState(w);

    const int newPhysW = toPhysW(w->width);
    const int newPhysH = toPhysH(w->height);
    const int newPhysX = toPhysX(w->x);
    const int newPhysY = toPhysY(w->y);

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

    w->vp_.fitToView();
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
    isFocusable = true; // focus flows through the normal Widget/FluxUI focus
                        // system now — no PlatformWindow-side focusedCanvas_.
}

CanvasWidget::~CanvasWidget()
{
    destroyCanvas(this);
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
    scheduleRepaint(this);
}

// render() now does everything: lazy-init, geometry sync, surface tick,
// and the actual Cairo draw — all inline, in one call, on the normal
// widget-tree paint pass. No external pre/gl split, no separate driver.
void CanvasWidget::render(GraphicsContext &ctx, FontCache &)
{
    if (!ctx.cr)
    {
        needsPaint = false;
        return;
    }

    auto &s = linuxState(this);

    if (!s.initialized)
        initCanvas(this);
    if (!s.initialized)
    {
        // Backend not available yet (e.g. very first frame before
        // PlatformWindow::create finished). Try again next paint.
        needsPaint = false;
        return;
    }

    syncPhysicalGeometry(this);

    if (pendingSurface_)
        activatePendingSurface();
    if (!activeSurface_)
    {
        needsPaint = false;
        return;
    }

    // ── tick ──
    auto now = Clock::now();
    frameDt_ = std::chrono::duration<double>(now - lastTick_).count();
    lastTick_ = now;
    activeSurface_->update(frameDt_);
    activeSurface_->preRender();

    // ── draw ──
    s.repaintPending = false;

    cairo_t *cr = ctx.cr;

    cairo_save(cr);
    cairo_rectangle(cr, (double)x, (double)y, (double)width, (double)height);
    cairo_clip(cr);

    cairo_translate(cr, (double)x, (double)y);
    float z = vp_.zoom();
    float ox = -vp_.offsetX() * z;
    float oy = -vp_.offsetY() * z;
    cairo_translate(cr, ox, oy);
    cairo_scale(cr, z, z);

    {
        Canvas2D canvasCtx(backend_, canvasW_, canvasH_);
        Canvas2D_setCairo(canvasCtx, cr);
        activeSurface_->render(canvasCtx);
    }

    cairo_restore(cr);

    if (scrollbarsEnabled_)
    {
        updateSBGeometry(s.lastW, s.lastH);
        hBar_.tick(frameDt_);
        vBar_.tick(frameDt_);

        GraphicsContext gctx(cr, width, height);
        Painter p(gctx);
        p.drawScrollbar(hBar_, s.lastW, s.lastH);
        p.drawScrollbar(vBar_, s.lastW, s.lastH);

        if ((hBar_.needsRedraw() || vBar_.needsRedraw()) && !s.repaintPending)
            scheduleRepaint(this);
    }

    if (activeSurface_->needsContinuousRedraw())
        scheduleRepaint(this);

    needsPaint = false;
}

void CanvasWidget::onDetach()
{
    destroyCanvas(this);
    Widget::onDetach();
}

void CanvasWidget::markNeedsPaint()
{
    Widget::markNeedsPaint();
    scheduleRepaint(this);
}

void CanvasWidget::onWindowResize(int /*newW*/, int /*newH*/)
{
    // No longer called externally by PlatformWindow (it has no idea we
    // exist). Kept as a public method in case something else wants to
    // force-invalidate cached geometry; normal resizes are now picked up
    // automatically by syncPhysicalGeometry() on the next render().
    auto &s = linuxState(this);
    if (!s.initialized)
        return;
    s.lastW = 0;
    s.lastH = 0;
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
    if (panning_)
        panning_ = false; // cancel any in-progress pan — PlatformWindow no
                          // longer does this for us on window-leave/focus-lost
    scheduleRepaint(this);
}

// ─────────────────────────────────────────────────────────────────────────────
// Input — real Widget overrides, reached through the normal dispatch chain
// in flux_core.cpp (findAndHandleMouseEvent / broadcastMouseEvent /
// focusedWidget). Coordinates arrive in window space; we translate to
// widget-local ourselves, same convention as the Win32 implementation.
// ─────────────────────────────────────────────────────────────────────────────

bool CanvasWidget::handleMouseDown(int mx, int my)
{
    int lx = mx - x;
    int ly = my - y;

    if (auto *inst = FluxUI::getCurrentInstance())
        inst->captureMouseInput();

    bool hC = hBar_.onMouseDown(lx, ly, [this](float t)
                                { applyHScrollFraction(t); });
    bool vC = !hC && vBar_.onMouseDown(lx, ly, [this](float t)
                                       { applyVScrollFraction(t); });
    if (!hC && !vC)
    {
        if (viewportEnabled_ && platformSpaceDown())
        {
            beginPan(lx, ly);
        }
        else if (activeSurface_)
        {
            auto [cx, cy] = vp_.screenToCanvas(float(lx), float(ly));
            activeSurface_->onMouseDown(cx, cy);
        }
    }
    scheduleRepaint(this);
    return true;
}

bool CanvasWidget::handleMouseMove(int mx, int my)
{
    int lx = mx - x;
    int ly = my - y;

    bool hDrag = hBar_.onMouseMove(lx, ly);
    bool vDrag = vBar_.onMouseMove(lx, ly);
    scheduleRepaint(this);
    if (hDrag || vDrag)
        return true;

    if (panning_)
    {
        continuePan(lx, ly);
    }
    else if (activeSurface_)
    {
        auto [cx, cy] = vp_.screenToCanvas(float(lx), float(ly));
        activeSurface_->onMouseMove(cx, cy);
    }
    return true;
}

bool CanvasWidget::handleMouseUp(int mx, int my)
{
    int lx = mx - x;
    int ly = my - y;

    if (auto *inst = FluxUI::getCurrentInstance())
        inst->releaseMouseInput();

    bool hR = hBar_.onMouseUp(lx, ly);
    bool vR = vBar_.onMouseUp(lx, ly);
    if (!hR && !vR)
    {
        if (panning_)
        {
            panning_ = false;
        }
        else if (activeSurface_)
        {
            auto [cx, cy] = vp_.screenToCanvas(float(lx), float(ly));
            activeSurface_->onMouseUp(cx, cy);
        }
    }
    scheduleRepaint(this);
    return true;
}

bool CanvasWidget::handleMouseWheel(int delta)
{
    if (!viewportEnabled_)
        return false;

    bool ctrl = platformCtrlDown();
    bool shift = platformShiftDown();

    if (ctrl)
    {
        float f = (delta > 0) ? 1.1f : 1.f / 1.1f;
        // SDL_MOUSEWHEEL carries no cursor position, so — same limitation
        // Win32's WM_MOUSEWHEEL has — zoom toward the widget's own center.
        vp_.zoomToward(float(width) * 0.5f, float(height) * 0.5f, f);
    }
    else if (shift)
    {
        vp_.panByScreen(float(delta) * 0.5f, 0.f);
    }
    else
    {
        vp_.panByScreen(0.f, -(float(delta) / float(WHEEL_DELTA)) * 40.f);
    }
    pokeScrollbars();
    scheduleRepaint(this);
    return true;
}

bool CanvasWidget::handleKeyDown(int keyCode)
{
    bool ctrl = platformCtrlDown();
    bool shift = platformShiftDown();
    bool consumed = false;

    if (ctrl && viewportEnabled_)
    {
        if (keyCode == SDLK_EQUALS || keyCode == SDLK_PLUS || keyCode == SDLK_KP_PLUS)
        {
            vp_.zoomIn();
            pokeScrollbars();
            scheduleRepaint(this);
            consumed = true;
        }
        else if (keyCode == SDLK_MINUS || keyCode == SDLK_KP_MINUS)
        {
            vp_.zoomOut();
            pokeScrollbars();
            scheduleRepaint(this);
            consumed = true;
        }
        else if (keyCode == SDLK_0 || keyCode == SDLK_KP_0)
        {
            vp_.resetZoom();
            pokeScrollbars();
            scheduleRepaint(this);
            consumed = true;
        }
    }

    if (!consumed && activeSurface_)
    {
        KeyEvent ke{0, keyCode, ctrl, shift, false};
        activeSurface_->onKeyDown(ke);
    }
    return consumed;
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

#endif