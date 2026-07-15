// include/flux/flux_core.hpp
#ifndef FLUX_CORE_HPP 
#define FLUX_CORE_HPP

#include "flux_font.hpp"
#include "flux_keys.hpp"
#include "flux_layoutengine.hpp"
#include "flux_renderer.hpp"
#include "flux_widget.hpp"
#include "flux_window.hpp"


#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

template <typename T>
class State;

// ============================================================================
// OVERLAY LAYER
// ============================================================================
// Replaces the old OverlayManager/OverlayContent split. Floating content
// (dropdown lists, menus, tooltips, dialogs) is now an ordinary Widget with
// real absolute x/y/width/height, rendered and dispatched last so it paints
// on top and gets first shot at input. No popup windows, no per-platform
// coordinate translation — every Painter backend already resolves absolute
// coordinates correctly on its own.
struct OverlayEntry
{
  Widget *widget = nullptr;
  int zIndex = 0;
  bool modal = false;            // swallows clicks outside; blocks tree below
  bool blocksHoverBelow = false; // stops hover/move reaching the tree below
  bool capturesKeyboard = true;  // gets first shot at key events while open
  bool pendingRemoval = false;   // set by hideOverlay() during dispatch
};

// ============================================================================
// MOUSE EVENT BROADCAST HELPERS
// ============================================================================

inline bool
broadcastMouseEvent(Widget *widget, int x, int y,
                    std::function<bool(Widget *, int, int)> handler)
{
  if (!widget)
    return false;
  if (handler(widget, x, y))
    return true;
  for (auto &child : widget->children)
    if (broadcastMouseEvent(child.get(), x, y, handler))
      return true;
  return false;
}

// ============================================================================
// FLUXUI CLASS
// ============================================================================

class FluxUI
{
private:
  WidgetPtr root;
  std::function<WidgetPtr()> builder;
  FontCache fontCache;
  AppInstance hInstance;
  PlatformWindow window;
  Widget *focusedWidget = nullptr;

  // thread_local, not a plain global — see flux_core.cpp for the full
  // rationale. A single browser tab or native app only ever has one
  // thread doing UI work, so this behaves exactly as a plain global did
  // before. It matters once multiple FluxUI instances render concurrently
  // on different threads (e.g. an SSR host handling several requests at
  // once) — each thread must resolve its OWN "currently active" instance,
  // or ambient lookups like State<T>'s single-arg constructor and
  // FlexWidget::resolveProps() could silently read another thread's app.
  static thread_local FluxUI *currentInstance;

  std::unordered_map<const void *, std::shared_ptr<void>> appSingletons_;

  std::mutex pendingRebuildsMutex_;
  std::vector<Widget *> pendingRebuilds_;

  // Floating overlay content (dropdowns, menus, tooltips, dialogs). Replaces
  // the old OverlayManager object — just a sorted vector of Widget* plus
  // policy flags now, no separate popup-managing subsystem.
  std::vector<OverlayEntry> overlayLayer_;
  int overlayDispatchDepth_ = 0;
  struct OverlayDispatchScope;

  OverlayEntry *findOverlay(Widget *widget);
  void sortOverlaysByZ();
  void pruneRemovedOverlays_();

  Widget *findLayoutBoundary(Widget *widget);
  WidgetPtr findByIdRecursive(WidgetPtr widget, const std::string &id);
  void wireCallbacks();

public:
  std::map<TimerID, std::function<void()>> timerCallbacks;
  std::vector<std::pair<TimerID, std::function<void()>>> pendingTimers;

  explicit FluxUI(AppInstance hInst);
  ~FluxUI();

  static FluxUI *getCurrentInstance();

  TimerID setInterval(int ms, std::function<void()> callback);
  void clearInterval(TimerID id);

  template <typename T>
  State<T> useState(T initialValue);

  void setFocus(Widget *widget);
  Widget *getFocusedWidget() const;

  void build(std::function<WidgetPtr()> buildFunc);
  void rebuild();
  void updateWidget(Widget *widget);
  void invalidateWidget(Widget *widget);
  void partialRebuild(Widget *widget);

  NativeWindow createWindow(const std::string &title, int width, int height);
  int run();

  NativeWindow getWindow() const;
  WidgetPtr getRoot() const;
  WidgetPtr findById(const std::string &id);
  FontCache &getFontCache();

  void setClipboardText(const std::string &text);
  std::string getClipboardText();
  void invalidateWidget(int x, int y, int w, int h); // rect overload

  void captureMouseInput();
  void releaseMouseInput();
  MeasureContext getMeasureContext();

  void postToRenderThread(std::function<void()> fn);

  PlatformWindow::ScreenPoint clientToScreen(int cx, int cy) const;
  PlatformWindow::ScreenPoint screenToClient(int sx, int sy) const;
  PlatformWindow::ClientSize getClientSize() const;
  PlatformWindow &getPlatformWindow() { return window; }

  PlatformWindow *getPlatformWindowPtr()
  {
    return &window;
  }

  void setResizeCursorH();
  void setResizeCursorV();
  void setDefaultCursor();

  void scheduleRebuild(Widget *widget);
  void drainPendingRebuilds();

  // ── Overlay layer ─────────────────────────────────────────────────────
  // Floating widgets call these directly at open/close time, e.g.
  //   FluxUI::getCurrentInstance()->showOverlay(this, 100);
  // "widget" is expected to already have real absolute x/y/width/height —
  // showOverlay only adds it to the paint/dispatch layer, it does no
  // positioning of its own.
  void showOverlay(Widget *widget, int zIndex,
                   bool modal = true, bool blocksHoverBelow = false,
                   bool capturesKeyboard = true);
  void hideOverlay(Widget *widget);
  void refreshOverlay(Widget *widget); // invalidates the widget's own rect;
                                       // kept for call-site symmetry with
                                       // the old refresh()
  bool isOverlayOpen(Widget *widget) const;
  void closeAllOverlays();
  bool overlayHasBlocking() const;

  bool dispatchOverlayMouseDown(int clientX, int clientY);
  bool dispatchOverlayMouseUp(int clientX, int clientY);
  bool dispatchOverlayMouseMove(int clientX, int clientY);
  bool dispatchOverlayMouseWheel(int delta);
  bool dispatchOverlayKeyDown(int keyCode);
  bool dispatchOverlayRightClick(int clientX, int clientY);

  template <typename T>
  std::shared_ptr<T>
  getOrCreateSingleton(std::function<std::shared_ptr<T>()> factory)
  {
    // One static per T instantiation → unique address per type.
    static const char typeTag = 0;
    const void *key = static_cast<const void *>(&typeTag);

    auto it = appSingletons_.find(key);
    if (it == appSingletons_.end())
    {
      auto inst = factory();
      appSingletons_[key] = inst;
      return inst;
    }
    return std::static_pointer_cast<T>(it->second);
  }
};

#endif // FLUX_CORE_HPP