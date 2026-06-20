#include "flux/flux_core.hpp"
#include "flux/flux_app.hpp"

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
    window.callbacks.onPaint = [this](GraphicsContext &ctx, int /*w*/, int /*h*/)
    {
        if (!root)
            return;
        Renderer::renderWidget(ctx, root.get(), fontCache);
        overlayMgr_.renderAll(ctx, fontCache); // non-Win32: paints open overlays inline
    };

    window.callbacks.onResize = [this](GraphicsContext &ctx, int w, int h)
    {
        if (!root)
            return;
        LayoutEngine::computeLayout(ctx, root.get(), w, h, fontCache);
        LayoutEngine::positionWidget(root.get(), 0, 0);
    };

    window.callbacks.onMouseDown = [this](int x, int y) -> bool
    {
        if (!root)
            return false;
        if (overlayMgr_.dispatchMouseDown(x, y))
            return true;
        return findAndHandleMouseEvent(root.get(), x, y, [x, y, this](Widget *w)
                                       {
            bool h = w->handleMouseDown(x, y);
            if (h && w->isFocusable) setFocus(w);
            return h; });
    };

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
            return overlay; // modal overlay open — don't touch the tree underneath

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
        if (overlayMgr_.dispatchMouseWheel(delta))
            return true;
        return focusedWidget && focusedWidget->handleMouseWheel(delta);
    };

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

    window.callbacks.onKeyDown = [this](int keyCode) -> bool
    {
        if (overlayMgr_.dispatchKeyDown(keyCode))
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
        auto cb = it->second;
        cb();
    };

    window.callbacks.onNonClientMouseDown = [this]()
    { setFocus(nullptr); };
    window.callbacks.onFocusLost = [this]()
    { setFocus(nullptr); };
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
        window.setTimer(id, ms);
    else
        pendingTimers.push_back(
            {id, [this, id, ms]()
             { window.setTimer(id, ms); }});
    return id;
}

void FluxUI::clearInterval(TimerID id)
{
    window.killTimer(id);
    timerCallbacks.erase(id);
    pendingTimers.erase(
        std::remove_if(pendingTimers.begin(), pendingTimers.end(),
                       [id](const std::pair<TimerID, std::function<void()>> &p)
                       {
                           return p.first == id;
                       }),
        pendingTimers.end());
}

// ============================================================================
// BUILD / REBUILD
// ============================================================================

void FluxUI::build(std::function<WidgetPtr()> buildFunc)
{
    window.startupGdiplus();
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

    // Close anything still open from the tree we're about to discard.
    overlayMgr_.closeAll();

    root = builder();
  

    if (window.valid())
    {
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
    wireCallbacks();
    window.create(title, w, h, hInstance, &window);
    if (root)
    {
        auto mc = getMeasureContext();
        LayoutEngine::computeLayout(mc.ctx, root.get(),
                                    window.clientWidth(), window.clientHeight(),
                                    fontCache);
        LayoutEngine::positionWidget(root.get(), 0, 0);
    }
    for (auto &[id, fn] : pendingTimers)
        fn();
    pendingTimers.clear();
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

// ============================================================================
// getMeasureContext — platform branch
// ============================================================================

MeasureContext FluxUI::getMeasureContext()
{
#ifdef _WIN32
    return MeasureContext(window.handle());

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
    // iOS / other Apple — fallback
    return MeasureContext(nullptr, 0, 0);
#endif

#elif defined(__EMSCRIPTEN__)
    GraphicsContext gc = window.getMeasureContext();
    return MeasureContext(gc.width, gc.height);
#else
    return MeasureContext(nullptr, 0, 0);
#endif
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

