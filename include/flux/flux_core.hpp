#ifndef FLUX_CORE_HPP
#define FLUX_CORE_HPP

#include "flux_font.hpp"
#include "flux_layoutengine.hpp"
#include "flux_renderer.hpp"
#include "flux_widget.hpp"
#include "flux_window.hpp"
#include "flux_keys.hpp"

#include <functional>
#include <map>
#include <string>
#include <vector>

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

template <typename T> class State;
class ScaffoldWidget;

// ============================================================================
// MOUSE EVENT BROADCAST HELPERS
// ============================================================================

inline bool
broadcastMouseEvent(Widget *widget, int x, int y,
                    std::function<bool(Widget *, int, int)> handler) {
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

class FluxUI {
private:
  WidgetPtr root;
  std::function<WidgetPtr()> builder;
  FontCache fontCache;
  AppInstance hInstance;
  PlatformWindow window;
  Widget *focusedWidget = nullptr;

  static FluxUI *currentInstance;

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

  void wireCallbacks();

public:
  std::map<TimerID, std::function<void()>> timerCallbacks;
  std::vector<std::pair<TimerID, std::function<void()>>> pendingTimers;

  explicit FluxUI(AppInstance hInst);
  ~FluxUI();

  static FluxUI *getCurrentInstance();

  TimerID setInterval(int ms, std::function<void()> callback);
  void clearInterval(TimerID id);

  template <typename T> State<T> useState(T initialValue);

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

  void setResizeCursorH(); // horizontal resize (SIZEWE)
  void setResizeCursorV(); // vertical resize   (SIZENS)
  void setDefaultCursor(); // arrow
};

#endif // FLUX_CORE_HPP