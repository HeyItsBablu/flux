#ifndef FLUX_CORE_HPP
#define FLUX_CORE_HPP

#include "flux_font.hpp"
#include "flux_layoutengine.hpp"
#include "flux_renderer.hpp"
#include "flux_widget.hpp"

#include <map>
#include <tuple>
#include <windowsx.h>

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================
 
template <typename T> class State;
class ScaffoldWidget;

// ============================================================================
// MOUSE EVENT BROADCAST HELPERS (for captured mouse events)
// ============================================================================

inline bool
broadcastMouseEvent(Widget *widget, int x, int y,
                    std::function<bool(Widget *, int, int)> handler) {
  if (!widget)
    return false;

  if (handler(widget, x, y))
    return true;

  for (auto &child : widget->children) {
    if (broadcastMouseEvent(child.get(), x, y, handler))
      return true;
  }

  return false;
}

// ============================================================================
// FLUXUI CLASS
// ============================================================================

class FluxUI {
private:
  WidgetPtr root;
  std::function<WidgetPtr()> builder;
  HWND hwnd = nullptr;
  HINSTANCE hInstance;
  FontCache fontCache;

  ULONG_PTR gdiplusToken = 0;

BackBuffer backBuffer;

  static FluxUI *currentInstance;

  Widget *focusedWidget = nullptr;



  // ----------------------------------------------------------------
  // OVERLAY DISPATCHERS
  // ----------------------------------------------------------------
  bool handleDropdownOverlays(int mouseX, int mouseY);
  bool handleDialogOverlays(int mouseX, int mouseY);
  bool handleOverlayMouseMove(int mouseX, int mouseY);
  bool handleOverlayMouseWheel(int delta);
  bool handleOverlayKeyDown(int keyCode);
  bool handleOverlayRightClick(int mouseX, int mouseY);

  // Legacy tree-walk stubs (no-op)
  bool checkDropdownOverlays(Widget *widget, int mouseX, int mouseY);
  bool checkDialogOverlays(Widget *widget, int mouseX, int mouseY);

  void wireScaffoldToWidgets(ScaffoldWidget *scaffold, Widget *widget);

  Widget *findLayoutBoundary(Widget *widget);

  // ----------------------------------------------------------------
  // WINDOW PROCEDURE
  // ----------------------------------------------------------------
  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam,
                                     LPARAM lParam);

  // ----------------------------------------------------------------
  // findByIdRecursive
  // ----------------------------------------------------------------
  WidgetPtr findByIdRecursive(WidgetPtr widget, const std::string &id);

public:
  // ----------------------------------------------------------------
  // Public data (timers)
  // ----------------------------------------------------------------
  std::map<UINT, std::function<void()>> timerCallbacks;
  std::vector<std::pair<UINT, std::function<void()>>> pendingTimers;

  // ----------------------------------------------------------------
  // Construction / destruction
  // ----------------------------------------------------------------
  explicit FluxUI(HINSTANCE hInst);
  ~FluxUI();

  // ----------------------------------------------------------------
  // Timer helpers
  // ----------------------------------------------------------------
  UINT setInterval(int ms, std::function<void()> callback);
  void clearInterval(UINT id);

  // ----------------------------------------------------------------
  // State factory
  // ----------------------------------------------------------------
  template <typename T> State<T> useState(T initialValue);

  // ----------------------------------------------------------------
  // Static accessor
  // ----------------------------------------------------------------
  static FluxUI *getCurrentInstance();

  // ----------------------------------------------------------------
  // Focus management
  // ----------------------------------------------------------------
  void    setFocus(Widget *widget);
  Widget *getFocusedWidget() const;

  // ----------------------------------------------------------------
  // Build / layout / invalidation
  // ----------------------------------------------------------------
  void build(std::function<WidgetPtr()> buildFunc);
  void rebuild();
  void updateWidget(Widget *widget);
  void invalidateWidget(Widget *widget);
  void partialRebuild(Widget *widget);

  // ----------------------------------------------------------------
  // Window management
  // ----------------------------------------------------------------
  HWND createWindow(const std::string &title, int width, int height);
  int  run();

  // ----------------------------------------------------------------
  // Accessors
  // ----------------------------------------------------------------
  HWND       getWindow()    const;
  WidgetPtr  getRoot()      const;
  WidgetPtr  findById(const std::string &id);
  FontCache &getFontCache();
};

#endif // FLUX_CORE_HPP