//include/flux/flux_core.hpp
#ifndef FLUX_CORE_HPP
#define FLUX_CORE_HPP

#include "flux_font.hpp"
#include "flux_keys.hpp"
#include "flux_layoutengine.hpp"
#include "flux_overlay_manager.hpp"
#include "flux_renderer.hpp"
#include "flux_widget.hpp"
#include "flux_window.hpp"

#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

template <typename T>
class State;


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

  static FluxUI *currentInstance;

  std::unordered_map<const void *, std::shared_ptr<void>> appSingletons_;



  // Owns everything overlay-related: native popups, z-order, input dispatch.
  // Widgets talk to it directly via overlays(); core never needs to know
  // which overlay widget types exist.
  OverlayManager overlayMgr_;

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

  // ── Overlays ────────────────────────────────────────────────────────────
  // Any widget implementing OverlayContent calls this directly — e.g.
  //   FluxUI::getCurrentInstance()->overlays().show(this, x, y, w, h, zIndex, fontCache);
  // Core has no per-type knowledge of dropdowns/menus/dialogs/tooltips;
  // adding a new overlay widget never requires touching this class.
  OverlayManager &overlays() { return overlayMgr_; }

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