#include "flux/flux_core.hpp"
#include "flux/flux_app.hpp"
#include "flux/flux_overlay_host.hpp"

// ============================================================================
// STATIC MEMBER DEFINITION
// ============================================================================

FluxUI *FluxUI::currentInstance = nullptr;

// ============================================================================
// CONSTRUCTION / DESTRUCTION
// ============================================================================

FluxUI::FluxUI(AppInstance hInst) : hInstance(hInst) { currentInstance = this; }

FluxUI::~FluxUI() {
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

Widget *FluxUI::findLayoutBoundary(Widget *widget) {
  Widget *boundary = widget;
  Widget *current = widget->parent;

  while (current) {
    boundary = current;
    if (!current->autoWidth && !current->autoHeight)
      break;
    current = current->parent;
  }
  return boundary;
}

WidgetPtr FluxUI::findByIdRecursive(WidgetPtr widget, const std::string &id) {
  if (!widget)
    return nullptr;

  if (widget->getId() == id)
    return widget;

  for (auto &child : widget->children) {
    auto found = findByIdRecursive(child, id);
    if (found)
      return found;
  }

  return nullptr;
}

// ============================================================================
// SCAFFOLD WIRING
// ============================================================================

void FluxUI::wireScaffoldToWidgets(ScaffoldWidget *scaffold, Widget *widget) {
  if (!widget)
    return;

  if (auto *host = dynamic_cast<OverlayHost *>(widget))
    host->setScaffold(scaffold);

  for (auto &child : widget->children)
    wireScaffoldToWidgets(scaffold, child.get());
}

// ============================================================================
// OVERLAY DISPATCHERS
// ============================================================================

namespace {
// File-local helper: depth-first search for a ScaffoldWidget.
ScaffoldWidget *findScaffold(Widget *widget) {
  if (!widget)
    return nullptr;
  if (auto *s = dynamic_cast<ScaffoldWidget *>(widget))
    return s;
  for (auto &child : widget->children) {
    if (auto *s = findScaffold(child.get()))
      return s;
  }
  return nullptr;
}
} // namespace

bool FluxUI::handleDropdownOverlays(int mouseX, int mouseY) {
  auto *scaffold = findScaffold(root.get());
  if (!scaffold || !scaffold->hasOverlays())
    return false;
  const auto &stack = scaffold->getOverlayStack();
  for (auto it = stack.rbegin(); it != stack.rend(); ++it)
    if (it->widget && it->widget->handleMouseDown(mouseX, mouseY))
      return true;
  return false;
}

bool FluxUI::handleDialogOverlays(int mouseX, int mouseY) {
  (void)mouseX;
  (void)mouseY;
  return false;
}

bool FluxUI::handleOverlayMouseMove(int mouseX, int mouseY) {
  auto *scaffold = findScaffold(root.get());
  if (!scaffold || !scaffold->hasOverlays())
    return false;
  const auto &stack = scaffold->getOverlayStack();
  for (auto it = stack.rbegin(); it != stack.rend(); ++it)
    if (it->widget && it->widget->handleMouseMove(mouseX, mouseY))
      return true;
  return false;
}

bool FluxUI::handleOverlayMouseWheel(int delta) {
  auto *scaffold = findScaffold(root.get());
  if (!scaffold || !scaffold->hasOverlays())
    return false;
  const auto &stack = scaffold->getOverlayStack();
  for (auto it = stack.rbegin(); it != stack.rend(); ++it)
    if (it->widget && it->widget->handleMouseWheel(delta))
      return true;
  return false;
}

bool FluxUI::handleOverlayKeyDown(int keyCode) {
  auto *scaffold = findScaffold(root.get());
  if (!scaffold || !scaffold->hasOverlays())
    return false;
  Widget *top = scaffold->getTopmostOverlay();
  return top ? top->handleKeyDown(keyCode) : false;
}

bool FluxUI::handleOverlayMouseUp(int mouseX, int mouseY) {
  auto *scaffold = findScaffold(root.get());
  if (!scaffold || !scaffold->hasOverlays())
    return false;
  const auto &stack = scaffold->getOverlayStack();
  for (auto it = stack.rbegin(); it != stack.rend(); ++it)
    if (it->widget && it->widget->handleMouseUp(mouseX, mouseY))
      return true;
  return false;
}

bool FluxUI::handleOverlayRightClick(int mouseX, int mouseY) {
  auto *scaffold = findScaffold(root.get());
  if (!scaffold || !scaffold->hasOverlays())
    return false;
  const auto &stack = scaffold->getOverlayStack();
  for (auto it = stack.rbegin(); it != stack.rend(); ++it)
    if (it->widget && it->widget->handleRightClick(mouseX, mouseY))
      return true;
  return false;
}

// ============================================================================
// CALLBACK WIRING — connects PlatformWindow events to FluxUI logic
// ============================================================================

void FluxUI::wireCallbacks() {
  window.callbacks.onPaint = [this](GraphicsContext &ctx, int /*w*/,
                                    int /*h*/) {
    if (!root)
      return;
    Renderer::renderWidget(ctx, root.get(), fontCache);
  };

  window.callbacks.onResize = [this](GraphicsContext &ctx, int w, int h) {
    if (!root)
      return;
    LayoutEngine::computeLayout(ctx, root.get(), w, h, fontCache);
    LayoutEngine::positionWidget(root.get(), 0, 0);
  };

  window.callbacks.onMouseWheel = [this](int delta) -> bool {
    if (!root)
      return false;
    if (handleOverlayMouseWheel(delta))
      return true;
    if (focusedWidget && focusedWidget->handleMouseWheel(delta))
      return true;
    return broadcastMouseEvent(root.get(), 0, 0, [delta](Widget *w, int, int) {
      return w->handleMouseWheel(delta);
    });
  };

  window.callbacks.onMouseDown = [this](int x, int y) -> bool {
    if (!root)
      return false;
    if (handleDropdownOverlays(x, y))
      return true;
    bool handled =
        findAndHandleMouseEvent(root.get(), x, y, [x, y, this](Widget *w) {
          bool h = w->handleMouseDown(x, y);
          if (h && w->isFocusable)
            setFocus(w);
          return h;
        });
    if (!handled) {
      setFocus(nullptr);
      Widget *clicked = findWidgetAt(root.get(), x, y);
      if (clicked && clicked->onClick) {
        clicked->onClick();
        return true;
      }
    }
    return handled;
  };

  window.callbacks.onMouseUp = [this](int x, int y) -> bool {
    if (!root)
      return false;
    if (handleOverlayMouseUp(x, y))
      return true;
    bool hasCaptured = window.isMouseCaptured();
    if (hasCaptured) {
      if (broadcastMouseEvent(root.get(), x, y, [](Widget *w, int mx, int my) {
            return w->handleMouseUp(mx, my);
          }))
        return true;
    }
    return findAndHandleMouseEvent(
        root.get(), x, y, [x, y](Widget *w) { return w->handleMouseUp(x, y); });
  };

  window.callbacks.onMouseMove = [this](int x, int y) -> bool {
    if (!root)
      return false;

    bool hasCaptured = window.isMouseCaptured();

    if (hasCaptured) {
      if (broadcastMouseEvent(root.get(), x, y, [](Widget *w, int mx, int my) {
            return w->handleMouseMove(mx, my);
          }))
        return true;
    }
    bool overlay = handleOverlayMouseMove(x, y);
    bool hover = updateHoverStates(root.get(), x, y);
    bool custom = findAndHandleMouseEvent(root.get(), x, y, [x, y](Widget *w) {
      return w->handleMouseMove(x, y);
    });
    return overlay || hover || custom;
  };

  window.callbacks.onMouseLeave = [this]() {
    if (root)
      root->clearHoverState();
  };

  window.callbacks.onRightClick = [this](int x, int y) -> bool {
    if (!root)
      return false;
    if (handleOverlayRightClick(x, y))
      return true;
    return findAndHandleMouseEvent(root.get(), x, y, [x, y](Widget *w) {
      return w->handleRightClick(x, y);
    });
  };

  window.callbacks.onKeyDown = [this](int keyCode) -> bool {
    if (handleOverlayKeyDown(keyCode))
      return true;
    return focusedWidget && focusedWidget->handleKeyDown(keyCode);
  };

  window.callbacks.onChar = [this](wchar_t ch) -> bool {
    return focusedWidget && focusedWidget->handleChar(ch);
  };

  window.callbacks.onTimer = [this](TimerID id) {
    auto it = timerCallbacks.find(id);
    if (it != timerCallbacks.end()) {
      it->second();
      return;
    }
    if (focusedWidget && focusedWidget->handleTimer(id))
      invalidateWidget(focusedWidget);
  };

  window.callbacks.onNonClientMouseDown = [this]() { setFocus(nullptr); };
  window.callbacks.onFocusLost = [this]() { setFocus(nullptr); };
}

// ============================================================================
// STATE FACTORY
// ============================================================================

template <typename T> State<T> FluxUI::useState(T initialValue) {
  return State<T>(initialValue, this);
}

// ============================================================================
// FOCUS MANAGEMENT
// ============================================================================

void FluxUI::setFocus(Widget *widget) {
  if (focusedWidget == widget)
    return;

  if (focusedWidget) {
    focusedWidget->handleFocus(false);
    invalidateWidget(focusedWidget);
  }

  focusedWidget = widget;

  if (focusedWidget) {
    focusedWidget->handleFocus(true);
    invalidateWidget(focusedWidget);
  }
}

Widget *FluxUI::getFocusedWidget() const { return focusedWidget; }

// ============================================================================
// TIMERS
// ============================================================================

TimerID FluxUI::setInterval(int ms, std::function<void()> callback) {
  static TimerID nextId = 100;
  TimerID id = nextId++;
  timerCallbacks[id] = callback;

  if (window.valid())
    window.setTimer(id, ms);
  else
    pendingTimers.push_back(
        {id, [this, id, ms]() { window.setTimer(id, ms); }});

  return id;
}

void FluxUI::clearInterval(TimerID id) {
  window.killTimer(id);
  timerCallbacks.erase(id);
  pendingTimers.erase(
      std::remove_if(pendingTimers.begin(), pendingTimers.end(),
                     [id](const std::pair<TimerID, std::function<void()>> &p) {
                       return p.first == id;
                     }),
      pendingTimers.end());
}

// ============================================================================
// BUILD / REBUILD
// ============================================================================

void FluxUI::build(std::function<WidgetPtr()> buildFunc) {
  window.startupGdiplus();
  builder = buildFunc;
  rebuild();
}

void FluxUI::rebuild() {
  if (!builder)
    return;

  if (root) {
    root->onDetach();
    if (auto *scaffold = findScaffold(root.get()))
      scaffold->clearOverlays();
  }

  root = builder();

  if (auto *scaffold = findScaffold(root.get()))
    wireScaffoldToWidgets(scaffold, root.get());

  if (window.valid()) {
    auto mc = getMeasureContext();
    LayoutEngine::computeLayout(mc.ctx, root.get(), window.clientWidth(),
                                window.clientHeight(), fontCache);
    LayoutEngine::positionWidget(root.get(), 0, 0);
    window.invalidate();
  }
}
// ============================================================================
// INVALIDATION / PARTIAL LAYOUT
// ============================================================================

void FluxUI::updateWidget(Widget *widget) {
  if (!widget || !window.valid())
    return;

  int oldWidth = widget->width;
  int oldHeight = widget->height;

  auto mc = getMeasureContext(); // ← already exists on FluxUI
  widget->measureText(mc.ctx, fontCache);
  widget->width += widget->paddingLeft + widget->paddingRight;
  widget->height += widget->paddingTop + widget->paddingBottom;

  if (oldWidth != widget->width || oldHeight != widget->height)
    partialRebuild(widget);
  else
    invalidateWidget(widget);
}

// ============================================================================
// INVALIDATION
// ============================================================================

void FluxUI::invalidateWidget(Widget *widget) {
  if (!widget)
    return;
  window.invalidateRect(widget->x, widget->y, widget->width, widget->height);
}

void FluxUI::partialRebuild(Widget *widget) {
  if (!widget || !window.valid())
    return;

  Widget *boundary = findLayoutBoundary(widget);
  Widget *current = widget;
  while (current && current != boundary) {
    current->markNeedsLayout();
    current = current->parent;
  }
  boundary->markNeedsLayout();

  auto mc = getMeasureContext(); // ← replaces GetDC/ReleaseDC block

  if (boundary == root.get()) {
    LayoutEngine::computeLayout(mc.ctx, root.get(), window.clientWidth(),
                                window.clientHeight(), fontCache);
    LayoutEngine::positionWidget(root.get(), 0, 0);
  } else {
    LayoutEngine::computeLayout(mc.ctx, boundary, boundary->width,
                                boundary->height, fontCache);
    LayoutEngine::positionWidget(boundary, boundary->x, boundary->y);
  }
  // MeasureContext destructor calls ReleaseDC automatically

  window.invalidateRect(boundary->x, boundary->y, boundary->width,
                        boundary->height);
}

// ============================================================================
// createWindow
// ============================================================================

NativeWindow FluxUI::createWindow(const std::string &title, int w, int h) {
  wireCallbacks();
  window.create(title, w, h, hInstance, &window);

  if (root) {
    auto mc = getMeasureContext(); // ← replaces GetDC/ReleaseDC block
    LayoutEngine::computeLayout(mc.ctx, root.get(), window.clientWidth(),
                                window.clientHeight(), fontCache);
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

WidgetPtr FluxUI::findById(const std::string &id) {
  return findByIdRecursive(root, id);
}

void FluxUI::setClipboardText(const std::string &t) {
  window.setClipboardText(t);
}
std::string FluxUI::getClipboardText() { return window.getClipboardText(); }

void FluxUI::invalidateWidget(int x, int y, int w, int h) {
  window.invalidateRect(x, y, w, h);
}

void FluxUI::captureMouseInput() { window.captureMouseInput(); }
void FluxUI::releaseMouseInput() { window.releaseMouseInput(); }

MeasureContext FluxUI::getMeasureContext() {
#ifdef _WIN32
  return MeasureContext(window.handle());
#elif defined(__linux__) && !defined(__ANDROID__)
  GraphicsContext gc = window.getMeasureContext();
  return MeasureContext(gc.cr, gc.width, gc.height);
#elif defined(__ANDROID__)
  GraphicsContext gc = window.getMeasureContext();
  return MeasureContext(gc.width, gc.height);
#endif
}

PlatformWindow::ScreenPoint FluxUI::clientToScreen(int cx, int cy) const {
  return window.clientToScreen(cx, cy);
}
PlatformWindow::ScreenPoint FluxUI::screenToClient(int sx, int sy) const {
  return window.screenToClient(sx, sy);
}
PlatformWindow::ClientSize FluxUI::getClientSize() const {
  return window.getClientSize();
}

void FluxUI::setResizeCursorH() { window.setResizeCursorH(); }
void FluxUI::setResizeCursorV() { window.setResizeCursorV(); }
void FluxUI::setDefaultCursor() { window.setDefaultCursor(); }

ScaffoldWidget *FluxUI::getRootScaffold() {
  return findScaffold(root.get()); // calls the file-local free function
}