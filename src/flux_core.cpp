#include "flux/flux_core.hpp"
#include "flux/flux_app.hpp"
#include "flux/flux_debug_log.hpp"

// Win32: we need RenderLoop to route timers through it.
#ifdef _WIN32
#include "flux/flux_render_loop.hpp"
#include "flux/flux_window.hpp"
#endif

// ============================================================================
// STATIC MEMBER DEFINITION
// ============================================================================

FluxUI *FluxUI::currentInstance = nullptr;

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
        if (!current->autoWidth && !current->autoHeight)
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
    fluxLog("[wireCallbacks] Step 1: entered");

    window.callbacks.onPaint = [this](GraphicsContext &ctx, int /*w*/, int /*h*/)
    {
        if (!root)
            return;
        fluxLog("[onPaint] root: x=" + std::to_string(root->x) +
                " y=" + std::to_string(root->y) +
                " w=" + std::to_string(root->width) +
                " h=" + std::to_string(root->height) +
                " children=" + std::to_string(root->children.size()));
        if (!root->children.empty())
        {
            auto *page = root->children[0].get();
            fluxLog("[onPaint] page child: x=" + std::to_string(page->x) +
                    " y=" + std::to_string(page->y) +
                    " w=" + std::to_string(page->width) +
                    " h=" + std::to_string(page->height));
        }
        Renderer::renderWidget(ctx, root.get(), fontCache);
        overlayMgr_.renderAll(ctx, fontCache);
    };
    fluxLog("[wireCallbacks] Step 2: onPaint wired");

    window.callbacks.onResize = [this](GraphicsContext &ctx, int w, int h)
    {
        if (!root)
            return;
        LayoutEngine::computeLayout(ctx, root.get(), w, h, fontCache);
        LayoutEngine::positionWidget(root.get(), 0, 0);
    };
    fluxLog("[wireCallbacks] Step 3: onResize wired");

    window.callbacks.onMouseDown = [this](int x, int y) -> bool
    {
        if (!root)
            return false;
        if (overlayMgr_.dispatchMouseDown(x, y))
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
    fluxLog("[wireCallbacks] Step 4: onMouseDown wired");

    window.callbacks.onMouseUp = [this](int x, int y) -> bool
    {
        if (!root)
            return false;
        if (overlayMgr_.dispatchMouseUp(x, y))
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
    fluxLog("[wireCallbacks] Step 5: onMouseUp wired");

    window.callbacks.onMouseMove = [this](int x, int y) -> bool
    {
        if (!root)
            return false;
        if (window.isMouseCaptured() &&
            broadcastMouseEvent(root.get(), x, y,
                                [](Widget *w, int mx, int my)
                                { return w->handleMouseMove(mx, my); }))
            return true;
        bool overlay = overlayMgr_.dispatchMouseMove(x, y);
        if (overlayMgr_.hasBlockingOverlay())
            return overlay;
        bool hover = updateHoverStates(root.get(), x, y);
        bool custom = findAndHandleMouseEvent(root.get(), x, y,
                                              [x, y](Widget *w)
                                              { return w->handleMouseMove(x, y); });
        return overlay || hover || custom;
    };
    fluxLog("[wireCallbacks] Step 6: onMouseMove wired");

    window.callbacks.onMouseWheel = [this](int delta) -> bool
    {
        if (!root)
            return false;
        if (overlayMgr_.dispatchMouseWheel(delta))
            return true;
        return focusedWidget && focusedWidget->handleMouseWheel(delta);
    };
    fluxLog("[wireCallbacks] Step 7: onMouseWheel wired");

    window.callbacks.onRightClick = [this](int x, int y) -> bool
    {
        if (!root)
            return false;
        if (overlayMgr_.dispatchRightClick(x, y))
            return true;
        return findAndHandleMouseEvent(root.get(), x, y,
                                       [x, y](Widget *w)
                                       { return w->handleRightClick(x, y); });
    };
    fluxLog("[wireCallbacks] Step 8: onRightClick wired");

    window.callbacks.onKeyDown = [this](int keyCode) -> bool
    {
        if (overlayMgr_.dispatchKeyDown(keyCode))
            return true;
        return focusedWidget && focusedWidget->handleKeyDown(keyCode);
    };
    fluxLog("[wireCallbacks] Step 9: onKeyDown wired");

    window.callbacks.onMouseLeave = [this]()
    {
        if (root)
            root->clearHoverState();
    };
    fluxLog("[wireCallbacks] Step 10: onMouseLeave wired");

    window.callbacks.onChar = [this](wchar_t ch) -> bool
    {
        return focusedWidget && focusedWidget->handleChar(ch);
    };
    fluxLog("[wireCallbacks] Step 11: onChar wired");

    window.callbacks.onTimer = [this](TimerID id)
    {
        auto it = timerCallbacks.find(id);
        if (it == timerCallbacks.end())
            return;
        it->second();
    };
    fluxLog("[wireCallbacks] Step 12: onTimer wired");

    window.callbacks.onNonClientMouseDown = [this]()
    { setFocus(nullptr); };
    fluxLog("[wireCallbacks] Step 13: onNonClientMouseDown wired");

    window.callbacks.onFocusLost = [this]()
    { setFocus(nullptr); };
    fluxLog("[wireCallbacks] Step 14: done");
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
    focusedWidget = nullptr;
    if (root)
        root->onDetach();

    overlayMgr_.closeAll();
    root = builder();

    if (window.valid())
    {
#ifdef _WIN32
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
        window.invalidate();
    }
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
    fluxLog("[invalidateWidget] x=" + std::to_string(widget->x) +
            " y=" + std::to_string(widget->y) +
            " w=" + std::to_string(widget->width) +
            " h=" + std::to_string(widget->height));
    window.invalidateRect(widget->x, widget->y, widget->width, widget->height);
}

void FluxUI::partialRebuild(Widget *widget)
{
    if (!widget || !window.valid())
        return;
    Widget *boundary = findLayoutBoundary(widget);
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

    window.invalidateRect(boundary->x, boundary->y,
                          boundary->width, boundary->height);
}

// ============================================================================
// createWindow
// ============================================================================

NativeWindow FluxUI::createWindow(const std::string &title, int w, int h)
{
    fluxLog("[createWindow] Step 1: entered title='" + title + "' " +
            std::to_string(w) + "x" + std::to_string(h));

    wireCallbacks();
    fluxLog("[createWindow] Step 2: wireCallbacks done");

    window.create(title, w, h, hInstance, &window);
    fluxLog("[createWindow] Step 3: window.create done, valid=" +
            std::to_string(window.valid()));

#ifdef _WIN32
    {
        auto ctx = window.getD2DContext();
        fluxLog("[createWindow] Step 4: dc=" +
                std::string(ctx.dc ? "valid" : "NULL") +
                " dwrite=" + std::string(ctx.dwrite ? "valid" : "NULL"));
        if (ctx.dwrite)
        {
            fontCache.setDWriteFactory(ctx.dwrite);
            fluxLog("[createWindow] Step 5: DWrite factory set");
        }
        else
        {
            fluxLog("[createWindow] Step 5: WARNING dwrite is NULL");
        }
    }
#endif

    if (root)
    {
        fluxLog("[createWindow] Step 6: running initial layout " +
                std::to_string(window.clientWidth()) + "x" +
                std::to_string(window.clientHeight()));

        auto mc = getMeasureContext();
        LayoutEngine::computeLayout(mc.ctx, root.get(),
                                    window.clientWidth(), window.clientHeight(),
                                    fontCache);
        LayoutEngine::positionWidget(root.get(), 0, 0);

        fluxLog("[createWindow] Step 7: layout done"
                " root: x=" +
                std::to_string(root->x) +
                " y=" + std::to_string(root->y) +
                " w=" + std::to_string(root->width) +
                " h=" + std::to_string(root->height));
    }
    else
    {
        fluxLog("[createWindow] Step 6: root is NULL, skipping layout");
    }

    fluxLog("[createWindow] Step 8: processing pendingTimers count=" +
            std::to_string(pendingTimers.size()));
    for (auto &[id, fn] : pendingTimers)
        fn();
    pendingTimers.clear();

    // Start render loop NOW — after layout is complete.
    // This prevents onLayout from firing while computeLayout is running
    // on the main thread, which caused the 0xC0000005 crash.
    fluxLog("[createWindow] Step 9: starting render loop");
    window.startRenderLoop();
    fluxLog("[createWindow] Step 10: done");

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
    fluxLog("[invalidateWidget rect] x=" + std::to_string(x) +
            " y=" + std::to_string(y) +
            " w=" + std::to_string(w) +
            " h=" + std::to_string(h));
    window.invalidateRect(x, y, w, h);
}

void FluxUI::captureMouseInput() { window.captureMouseInput(); }
void FluxUI::releaseMouseInput() { window.releaseMouseInput(); }



void FluxUI::scheduleRebuild(Widget* widget)
{
    std::lock_guard<std::mutex> lock(pendingRebuildsMutex_);
    pendingRebuilds_.push_back(widget);
    window.invalidate(); // thread-safe: just sets dirty = true
}

void FluxUI::drainPendingRebuilds()
{
    std::vector<Widget*> local;
    {
        std::lock_guard<std::mutex> lock(pendingRebuildsMutex_);
        local.swap(pendingRebuilds_);
    }
    for (Widget* w : local)
        partialRebuild(w); // safe: always called from render thread
}
// ============================================================================
// getMeasureContext  — platform branch
// ============================================================================

MeasureContext FluxUI::getMeasureContext()
{
#ifdef _WIN32
    // Borrow the always-live D2D device context.
    // No GetDC/ReleaseDC — D3DDevice owns the context for the window lifetime.
    // DWrite (used for text measurement) is documented thread-safe.
    auto ctx = window.getD2DContext();
    return MeasureContext(
        ctx.dc,
        ctx.dwrite,
        ctx.factory,
        ctx.brushes);

#elif defined(__linux__) && !defined(__ANDROID__)
    GraphicsContext gc = window.getMeasureContext();
    return MeasureContext(gc.cr, gc.width, gc.height);

#elif defined(__ANDROID__)
    GraphicsContext gc = window.getMeasureContext();
    return MeasureContext(gc.width, gc.height);

#elif defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX
    GraphicsContext gc = window.getMeasureContext();
    return MeasureContext(gc.cgContext, gc.width, gc.height);
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
#ifdef _WIN32
    RenderLoop* rl = window.getRenderLoop();
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
