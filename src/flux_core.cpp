#include "flux/flux_core.hpp"
#include "flux/flux_app.hpp"
#include <algorithm>

// Win32: we need RenderLoop to route timers through it.
#ifdef _WIN32
#include "flux/flux_render_loop.hpp"
#include "flux/flux_window.hpp"
#endif

// ============================================================================
// STATIC MEMBER DEFINITION
// ============================================================================

// thread_local storage — one "currently active FluxUI" per OS thread, not
// one per process. See the declaration in flux_core.hpp for why this
// matters for concurrent SSR rendering.
thread_local FluxUI *FluxUI::currentInstance = nullptr;

// ============================================================================
// CONSTRUCTION / DESTRUCTION
// ============================================================================

FluxUI::FluxUI(AppInstance hInst) : hInstance(hInst) { currentInstance = this; }

FluxUI::~FluxUI()
{
    fontCache.clear();
    if (currentInstance == this)
        currentInstance = nullptr;
}

// ============================================================================
// STATIC ACCESSOR
// ============================================================================

FluxUI *FluxUI::getCurrentInstance() { return currentInstance; }

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

Widget *FluxUI::findLayoutBoundary(Widget *widget)
{
    Widget *boundary = widget;
    Widget *current = widget->parent;
    while (current)
    {
        boundary = current;
        if (current->isLayoutBoundary())
            break;
        current = current->parent;
    }
    return boundary;
}

WidgetPtr FluxUI::findByIdRecursive(WidgetPtr widget, const std::string &id)
{
    if (!widget)
        return nullptr;
    if (widget->getId() == id)
        return widget;
    for (auto &child : widget->children)
    {
        auto found = findByIdRecursive(child, id);
        if (found)
            return found;
    }
    return nullptr;
}

// ============================================================================
// CALLBACK WIRING
// ============================================================================

void FluxUI::wireCallbacks()
{

    window.callbacks.onPaint = [this](GraphicsContext &ctx, int /*w*/, int /*h*/)
    {
        if (!root)
            return;

        std::lock_guard<std::recursive_mutex> lock(treeMutex_);
        if (!root->children.empty())
        {
            auto *page = root->children[0].get();
        }
        Renderer::renderWidget(ctx, root.get(), fontCache);
        // Paint floating overlay content last, in z order, so it sits on
        // top of the main tree. Every Painter backend resolves each
        // widget's own absolute x/y — no clip/transform/offset needed here,
        // unlike the old per-platform OverlayManager::renderAll().
        for (auto &e : overlayLayer_)
            if (!e.pendingRemoval)
                e.widget->render(ctx, fontCache);
    };

    window.callbacks.onResize = [this](GraphicsContext &ctx, int w, int h)
    {
        if (!root)
            return;
        std::lock_guard<std::recursive_mutex> lock(treeMutex_);
        LayoutEngine::computeLayout(ctx, root.get(), w, h, fontCache);
        LayoutEngine::positionWidget(root.get(), 0, 0);
    };

    window.callbacks.onMouseDown = [this](int x, int y) -> bool
    {
        if (!root)
            return false;
        std::lock_guard<std::recursive_mutex> lock(treeMutex_);
        if (dispatchOverlayMouseDown(x, y))
            return true;
        bool focusableHit = false;
        bool handled = findAndHandleMouseEvent(
            root.get(), x, y,
            [x, y, this, &focusableHit](Widget *w)
            {
                bool h = w->handleMouseDown(x, y);
                if (h && w->isFocusable)
                {
                    setFocus(w);
                    focusableHit = true;
                }
                return h;
            });
        // If no focusable widget handled this click, clear focus.
        // This ensures text inputs lose focus when the user clicks elsewhere.
        if (!focusableHit)
            setFocus(nullptr);
        return handled;
    };

    window.callbacks.onMouseUp = [this](int x, int y) -> bool
    {
        if (!root)
            return false;
        if (dispatchOverlayMouseUp(x, y))
            return true;
        if (window.isMouseCaptured() &&
            broadcastMouseEvent(root.get(), x, y,
                                [](Widget *w, int mx, int my)
                                { return w->handleMouseUp(mx, my); }))
            return true;
        return findAndHandleMouseEvent(root.get(), x, y,
                                       [x, y](Widget *w)
                                       { return w->handleMouseUp(x, y); });
    };

    window.callbacks.onMouseMove = [this](int x, int y) -> bool
    {
        if (!root)
            return false;
        lastMouseX_ = x;
        lastMouseY_ = y;
        std::lock_guard<std::recursive_mutex> lock(treeMutex_);
        if (window.isMouseCaptured() &&
            broadcastMouseEvent(root.get(), x, y,
                                [](Widget *w, int mx, int my)
                                { return w->handleMouseMove(mx, my); }))
            return true;
        bool overlay = dispatchOverlayMouseMove(x, y);
        if (overlayHasBlocking())
            return overlay;
        bool hover = updateHoverStates(root.get(), x, y);
        bool custom = findAndHandleMouseEvent(root.get(), x, y,
                                              [x, y](Widget *w)
                                              { return w->handleMouseMove(x, y); });
        return overlay || hover || custom;
    };

    window.callbacks.onMouseWheel = [this](int delta) -> bool
    {
        if (!root)
            return false;
        std::lock_guard<std::recursive_mutex> lock(treeMutex_);
        if (dispatchOverlayMouseWheel(delta))
            return true;
        // Wheel goes to whatever's under the cursor first — a scrollable
        // container doesn't need focus to scroll, same as every browser
        // and every native UI toolkit. Bubble up through parents since
        // the widget directly under the cursor (e.g. a Text/Image leaf)
        // is rarely the scrollable ancestor itself.
        if (Widget *w = findWidgetAt(root.get(), lastMouseX_, lastMouseY_))
        {
            for (; w; w = w->parent)
                if (w->handleMouseWheel(delta))
                    return true;
        }
        return focusedWidget && focusedWidget->handleMouseWheel(delta);
    };

    window.callbacks.onRightClick = [this](int x, int y) -> bool
    {
        if (!root)
            return false;
        if (dispatchOverlayRightClick(x, y))
            return true;
        return findAndHandleMouseEvent(root.get(), x, y,
                                       [x, y](Widget *w)
                                       { return w->handleRightClick(x, y); });
    };

    window.callbacks.onKeyDown = [this](int keyCode) -> bool
    {
        if (dispatchOverlayKeyDown(keyCode))
            return true;
        return focusedWidget && focusedWidget->handleKeyDown(keyCode);
    };

    window.callbacks.onMouseLeave = [this]()
    {
        if (root)
            root->clearHoverState();
    };

    window.callbacks.onChar = [this](wchar_t ch) -> bool
    {
        return focusedWidget && focusedWidget->handleChar(ch);
    };

    window.callbacks.onTimer = [this](TimerID id)
    {
        auto it = timerCallbacks.find(id);
        if (it == timerCallbacks.end())
            return;
        it->second();
    };

    window.callbacks.onDrainRebuilds = [this]()
    { drainPendingRebuilds(); };

    window.callbacks.onNonClientMouseDown = [this]()
    { setFocus(nullptr); };

    window.callbacks.onFocusLost = [this]()
    { setFocus(nullptr); };

    window.callbacks.onGLContextLost = [this]()
    {
        if (root)
            root->onGLContextLost();
    };
}

// ============================================================================
// STATE FACTORY
// ============================================================================

template <typename T>
State<T> FluxUI::useState(T initialValue)
{
    return State<T>(initialValue, this);
}

// ============================================================================
// FOCUS MANAGEMENT
// ============================================================================

void FluxUI::setFocus(Widget *widget)
{
    if (focusedWidget == widget)
        return;
    if (focusedWidget)
    {
        focusedWidget->handleFocus(false);
        invalidateWidget(focusedWidget);
    }
    focusedWidget = widget;
    if (focusedWidget)
    {
        focusedWidget->handleFocus(true);
        invalidateWidget(focusedWidget);
    }
}

Widget *FluxUI::getFocusedWidget() const { return focusedWidget; }

// ============================================================================
// TIMERS
// ============================================================================

TimerID FluxUI::setInterval(int ms, std::function<void()> callback)
{
    static TimerID nextId = 100;
    TimerID id = nextId++;
    timerCallbacks[id] = callback;

    if (window.valid())
    {
        // On Win32 this routes through RenderLoop (sub-frame accuracy,
        // render-thread execution, no Win32 15 ms floor).
        // On other platforms it calls SDL_AddTimer / ::SetTimer as before.
        window.setTimer(id, ms);
    }
    else
    {
        // Window not yet created — defer until createWindow() is called.
        pendingTimers.push_back(
            {id, [this, id, ms]()
             { window.setTimer(id, ms); }});
    }
    return id;
}

void FluxUI::clearInterval(TimerID id)
{
    window.killTimer(id);
    timerCallbacks.erase(id);
    pendingTimers.erase(
        std::remove_if(pendingTimers.begin(), pendingTimers.end(),
                       [id](const std::pair<TimerID, std::function<void()>> &p)
                       { return p.first == id; }),
        pendingTimers.end());
}

// ============================================================================
// BUILD / REBUILD
// ============================================================================

void FluxUI::build(std::function<WidgetPtr()> buildFunc)
{

    builder = buildFunc;
    rebuild();
}

void FluxUI::rebuild()
{
    if (!builder)
        return;
    {
        std::lock_guard<std::recursive_mutex> lock(treeMutex_);
        focusedWidget = nullptr;
        if (root)
            root->onDetach();

        closeAllOverlays();
        root = builder();

        if (window.valid())
        {
#if defined(_WIN32) && !defined(FLUX_SSR)
            // Re-confirm factory in case FontCache was cleared after device loss.
            {
                auto ctx = window.getD2DContext();
                if (ctx.dwrite)
                    fontCache.setDWriteFactory(ctx.dwrite);
            }
#endif
            auto mc = getMeasureContext();
            LayoutEngine::computeLayout(mc.ctx, root.get(),
                                        window.clientWidth(), window.clientHeight(),
                                        fontCache);
            LayoutEngine::positionWidget(root.get(), 0, 0);
        }
    } // treeMutex_ released before invalidate() — SSR's invalidate() paints
      // synchronously and re-enters treeMutex_ via onPaint on this same
      // thread; holding the lock across it self-deadlocks (EDEADLK).

    if (window.valid())
        window.invalidate();
}

// ============================================================================
// INVALIDATION / PARTIAL LAYOUT
// ============================================================================

void FluxUI::updateWidget(Widget *widget)
{
    if (!widget || !window.valid())
        return;
    int oldWidth = widget->width;
    int oldHeight = widget->height;
    auto mc = getMeasureContext();
    widget->measureText(mc.ctx, fontCache);
    widget->width += widget->paddingLeft + widget->paddingRight;
    widget->height += widget->paddingTop + widget->paddingBottom;
    if (oldWidth != widget->width || oldHeight != widget->height)
        partialRebuild(widget);
    else
        invalidateWidget(widget);
}

void FluxUI::invalidateWidget(Widget *widget)
{
    if (!widget)
        return;

    window.invalidateRect(widget->x, widget->y, widget->width, widget->height);
}

void FluxUI::partialRebuild(Widget *widget)
{
    if (!widget || !window.valid())
        return;
    Widget *boundary;
    {
        std::lock_guard<std::recursive_mutex> lock(treeMutex_);
        boundary = findLayoutBoundary(widget);
        Widget *current = widget;
        while (current && current != boundary)
        {
            current->markNeedsLayout();
            current = current->parent;
        }
        boundary->markNeedsLayout();

        auto mc = getMeasureContext();
        if (boundary == root.get())
        {
            LayoutEngine::computeLayout(mc.ctx, root.get(),
                                        window.clientWidth(), window.clientHeight(),
                                        fontCache);
            LayoutEngine::positionWidget(root.get(), 0, 0);
        }
        else
        {
            LayoutEngine::computeLayout(mc.ctx, boundary,
                                        boundary->width, boundary->height,
                                        fontCache);
            LayoutEngine::positionWidget(boundary, boundary->x, boundary->y);
        }
    } // treeMutex_ released before invalidateRect() — same self-deadlock
      // risk as rebuild() above under SSR's synchronous invalidate path.

    window.invalidateRect(boundary->x, boundary->y,
                          boundary->width, boundary->height);
}

// ============================================================================
// createWindow
// ============================================================================

NativeWindow FluxUI::createWindow(const std::string &title, int w, int h)
{

    wireCallbacks();

    window.create(title, w, h, hInstance, &window);

#if defined(_WIN32) && !defined(FLUX_SSR)
    {
        auto ctx = window.getD2DContext();

        if (ctx.dwrite)
        {
            fontCache.setDWriteFactory(ctx.dwrite);
        }
        else
        {
        }
    }
#endif

    if (root)
    {

        auto mc = getMeasureContext();
        LayoutEngine::computeLayout(mc.ctx, root.get(),
                                    window.clientWidth(), window.clientHeight(),
                                    fontCache);
        LayoutEngine::positionWidget(root.get(), 0, 0);
    }
    else
    {
    }

    for (auto &[id, fn] : pendingTimers)
        fn();
    pendingTimers.clear();

    // Start render loop NOW — after layout is complete.
    // This prevents onLayout from firing while computeLayout is running
    // on the main thread, which caused the 0xC0000005 crash.

    window.startRenderLoop();

    return window.handle();
}

int FluxUI::run() { return window.run(); }

// ============================================================================
// ACCESSORS
// ============================================================================

NativeWindow FluxUI::getWindow() const { return window.handle(); }
WidgetPtr FluxUI::getRoot() const { return root; }
FontCache &FluxUI::getFontCache() { return fontCache; }

WidgetPtr FluxUI::findById(const std::string &id)
{
    return findByIdRecursive(root, id);
}

void FluxUI::setClipboardText(const std::string &t) { window.setClipboardText(t); }
std::string FluxUI::getClipboardText() { return window.getClipboardText(); }

void FluxUI::invalidateWidget(int x, int y, int w, int h)
{

    window.invalidateRect(x, y, w, h);
}

void FluxUI::captureMouseInput() { window.captureMouseInput(); }
void FluxUI::releaseMouseInput() { window.releaseMouseInput(); }

void FluxUI::scheduleRebuild(Widget *widget)
{
    std::lock_guard<std::mutex> lock(pendingRebuildsMutex_);
    pendingRebuilds_.push_back(widget);
    window.invalidate(); // thread-safe: just sets dirty = true
}

void FluxUI::drainPendingRebuilds()
{
    std::vector<Widget *> local;
    {
        std::lock_guard<std::mutex> lock(pendingRebuildsMutex_);
        local.swap(pendingRebuilds_);
    }
    for (Widget *w : local)
        partialRebuild(w); // safe: always called from render thread
}
// ============================================================================
// getMeasureContext  — platform branch
// ============================================================================

MeasureContext FluxUI::getMeasureContext()
{
#if defined(FLUX_SSR)
    GraphicsContext gc = window.getMeasureContext();
    return MeasureContext(gc.width, gc.height);

#elif defined(_WIN32) && !defined(FLUX_SSR)
    auto ctx = window.getD2DContext();
    return MeasureContext(
        ctx.dc,
        ctx.dwrite,
        ctx.factory,
        ctx.brushes);

#elif defined(__linux__) && !defined(__ANDROID__) && !defined(FLUX_SSR)
    GraphicsContext gc = window.getMeasureContext();
    return MeasureContext(gc.cr, gc.width, gc.height);

#elif defined(__ANDROID__)
    GraphicsContext gc = window.getMeasureContext();
    return MeasureContext(gc.width, gc.height);

#elif defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX
    MeasureContext mc;
    mc.ctx = window.getMeasureContext();
    return mc;
#else
    return MeasureContext(nullptr, 0, 0);
#endif

#elif defined(__EMSCRIPTEN__)
    GraphicsContext gc = window.getMeasureContext();
    return MeasureContext(gc.width, gc.height);
#else
    return MeasureContext(nullptr, 0, 0);
#endif
}

void FluxUI::postToRenderThread(std::function<void()> fn)
{
#if defined(_WIN32) && !defined(FLUX_SSR)
    RenderLoop *rl = window.getRenderLoop();
    if (rl)
    {
        rl->post(std::move(fn));
        return;
    }
#endif
    // Fallback: run immediately if no render loop yet (shouldn't happen
    // in practice since images load after createWindow).
    fn();
}

// ============================================================================
// COORDINATE / CURSOR HELPERS
// ============================================================================

PlatformWindow::ScreenPoint FluxUI::clientToScreen(int cx, int cy) const
{
    return window.clientToScreen(cx, cy);
}

PlatformWindow::ScreenPoint FluxUI::screenToClient(int sx, int sy) const
{
    return window.screenToClient(sx, sy);
}

PlatformWindow::ClientSize FluxUI::getClientSize() const
{
    return window.getClientSize();
}

void FluxUI::setResizeCursorH() { window.setResizeCursorH(); }
void FluxUI::setResizeCursorV() { window.setResizeCursorV(); }
void FluxUI::setDefaultCursor() { window.setDefaultCursor(); }

// ============================================================================
// OVERLAY LAYER
// ============================================================================

struct FluxUI::OverlayDispatchScope
{
    FluxUI *ui;
    explicit OverlayDispatchScope(FluxUI *u) : ui(u) { ++ui->overlayDispatchDepth_; }
    ~OverlayDispatchScope()
    {
        if (--ui->overlayDispatchDepth_ == 0)
            ui->pruneRemovedOverlays_();
    }
};

static inline bool pointInWidget(Widget *w, int x, int y)
{
    return w && x >= w->x && x < w->x + w->width &&
           y >= w->y && y < w->y + w->height;
}

OverlayEntry *FluxUI::findOverlay(Widget *widget)
{
    for (auto &e : overlayLayer_)
        if (e.widget == widget && !e.pendingRemoval)
            return &e;
    return nullptr;
}

void FluxUI::sortOverlaysByZ()
{
    std::stable_sort(overlayLayer_.begin(), overlayLayer_.end(),
                     [](const OverlayEntry &a, const OverlayEntry &b)
                     { return a.zIndex < b.zIndex; });
}

void FluxUI::pruneRemovedOverlays_()
{
    overlayLayer_.erase(
        std::remove_if(overlayLayer_.begin(), overlayLayer_.end(),
                       [](const OverlayEntry &e)
                       { return e.pendingRemoval; }),
        overlayLayer_.end());
}

void FluxUI::showOverlay(Widget *widget, int zIndex,
                         bool modal, bool blocksHoverBelow, bool capturesKeyboard)
{
    if (!widget)
        return;
    OverlayEntry *e = findOverlay(widget);
    if (!e)
    {
        overlayLayer_.push_back({});
        e = &overlayLayer_.back();
        e->widget = widget;
    }
    e->zIndex = zIndex;
    e->modal = modal;
    e->blocksHoverBelow = blocksHoverBelow;
    e->capturesKeyboard = capturesKeyboard;
    e->pendingRemoval = false;
    sortOverlaysByZ();
    invalidateWidget(widget);
}

void FluxUI::hideOverlay(Widget *widget)
{
    OverlayEntry *e = findOverlay(widget);
    if (!e)
        return;
    e->pendingRemoval = true;
    if (overlayDispatchDepth_ == 0)
        pruneRemovedOverlays_();
    invalidateWidget(widget);
}

void FluxUI::refreshOverlay(Widget *widget)
{
    invalidateWidget(widget);
}

bool FluxUI::isOverlayOpen(Widget *widget) const
{
    for (auto &e : overlayLayer_)
        if (e.widget == widget && !e.pendingRemoval)
            return true;
    return false;
}

void FluxUI::closeAllOverlays()
{
    for (auto &e : overlayLayer_)
        e.pendingRemoval = true;
    if (overlayDispatchDepth_ == 0)
        pruneRemovedOverlays_();
}

bool FluxUI::overlayHasBlocking() const
{
    for (auto &e : overlayLayer_)
        if (!e.pendingRemoval && e.blocksHoverBelow)
            return true;
    return false;
}

bool FluxUI::dispatchOverlayMouseDown(int clientX, int clientY)
{
    OverlayDispatchScope scope(this);
    for (auto it = overlayLayer_.rbegin(); it != overlayLayer_.rend(); ++it)
    {
        if (it->pendingRemoval)
            continue;
        Widget *w = it->widget;
        if (pointInWidget(w, clientX, clientY))
        {
            if (w->handleMouseDown(clientX, clientY))
                return true;
        }
        else
        {
            w->onOverlayOutsideClick();
        }
        if (it->modal)
            return true;
    }
    return false;
}

bool FluxUI::dispatchOverlayMouseUp(int clientX, int clientY)
{
    OverlayDispatchScope scope(this);
    for (auto it = overlayLayer_.rbegin(); it != overlayLayer_.rend(); ++it)
    {
        if (it->pendingRemoval)
            continue;
        if (it->widget->handleMouseUp(clientX, clientY))
            return true;
    }
    return false;
}

bool FluxUI::dispatchOverlayMouseMove(int clientX, int clientY)
{
    OverlayDispatchScope scope(this);
    for (auto it = overlayLayer_.rbegin(); it != overlayLayer_.rend(); ++it)
    {
        if (it->pendingRemoval)
            continue;
        if (it->widget->handleMouseMove(clientX, clientY))
            return true;
    }
    return false;
}

bool FluxUI::dispatchOverlayMouseWheel(int delta)
{
    OverlayDispatchScope scope(this);
    for (auto it = overlayLayer_.rbegin(); it != overlayLayer_.rend(); ++it)
    {
        if (it->pendingRemoval)
            continue;
        if (it->widget->handleMouseWheel(delta))
            return true;
    }
    return false;
}

bool FluxUI::dispatchOverlayKeyDown(int keyCode)
{
    OverlayDispatchScope scope(this);
    for (auto it = overlayLayer_.rbegin(); it != overlayLayer_.rend(); ++it)
    {
        if (it->pendingRemoval)
            continue;
        if (it->capturesKeyboard)
            return it->widget->handleKeyDown(keyCode);
    }
    return false;
}

bool FluxUI::dispatchOverlayRightClick(int clientX, int clientY)
{
    OverlayDispatchScope scope(this);
    for (auto it = overlayLayer_.rbegin(); it != overlayLayer_.rend(); ++it)
    {
        if (it->pendingRemoval)
            continue;
        if (it->widget->handleRightClick(clientX, clientY))
            return true;
    }
    return false;
}