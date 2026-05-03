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
#include <vector>

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

template <typename T>
class State;
class ScaffoldWidget;

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

  // ── Fix: getOrCreateSingleton type-safe key ────────────────────────────
  // The map value is void* to the type-erased singleton, keyed by a stable
  // per-type pointer.  We use the address of a function-local static — one
  // unique static exists per instantiation of the function template, giving
  // us a zero-cost, ABI-stable, collision-free key that works correctly
  // across DLL boundaries (unlike typeid(T).name() which is
  // implementation-defined and can collide or vary between translation units).
  // ──────────────────────────────────────────────────────────────────────────
  std::unordered_map<const void *, std::shared_ptr<void>> appSingletons_;

  // No Win32 types in any of these signatures
  void wireScaffoldToWidgets(ScaffoldWidget *scaffold, Widget *widget);
  Widget *findLayoutBoundary(Widget *widget);
  WidgetPtr findByIdRecursive(WidgetPtr widget, const std::string &id);

  bool handleDropdownOverlays(int x, int y);
  bool handleDialogOverlays(int x, int y);
  bool handleOverlayMouseMove(int x, int y);
  bool handleOverlayMouseWheel(int delta);
  bool handleOverlayKeyDown(int keyCode);
  bool handleOverlayRightClick(int x, int y);
  bool handleOverlayMouseUp(int x, int y);

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

  template <typename T>
  std::shared_ptr<T>
  getOrCreateSingleton(std::function<std::shared_ptr<T>()> factory)
  {
    // ── Fix: type-safe singleton key ────────────────────────────────────────
    // A function-template static local has exactly one address per type T
    // across the entire program (the linker folds identical instantiations).
    // Using that address as the map key gives us:
    //   • No string allocation or hashing of implementation-defined names
    //   • Correct behaviour across DLL boundaries (typeid(T).name() is not
    //     guaranteed unique there)
    //   • Zero risk of accidental collision between two types whose
    //     typeid().name() strings happen to be the same
    // ────────────────────────────────────────────────────────────────────────
    static const int typeTag = 0;           // one per T, address is the key
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

  ScaffoldWidget *getRootScaffold();
};

#endif // FLUX_CORE_HPP