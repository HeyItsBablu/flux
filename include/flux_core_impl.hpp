#ifndef FLUX_CORE_IMPL_HPP
#define FLUX_CORE_IMPL_HPP

#include "flux_app.hpp"
#include "flux_core.hpp"
#include "flux_dialog.hpp"
#include "flux_dropdown.hpp"

// ============================================================================
// FLUXUI METHOD IMPLEMENTATIONS
// ============================================================================

// ----------------------------------------------------------------------------
// wireFluxAppToWidgets
// Walk the entire widget tree and hand every overlay-capable widget a pointer
// to the FluxAppWidget so they can call addOverlay / removeOverlay.
// Add new widget types here as they are introduced (tooltip, context menu…).
// ----------------------------------------------------------------------------

inline void FluxUI::wireFluxAppToWidgets(FluxAppWidget *fluxApp,
                                         Widget *widget) {
  if (!widget)
    return;

  if (auto *dropdown = dynamic_cast<DropdownWidget *>(widget))
    dropdown->setFluxApp(fluxApp);

  if (auto *dialog = dynamic_cast<DialogWidget *>(widget))
    dialog->setFluxApp(fluxApp);

  // Future widgets that need overlay access go here, e.g.:
  //   if (auto *tooltip = dynamic_cast<TooltipWidget *>(widget))
  //       tooltip->setFluxApp(fluxApp);
  //   if (auto *ctxMenu = dynamic_cast<ContextMenuWidget *>(widget))
  //       ctxMenu->setFluxApp(fluxApp);

  for (auto &child : widget->children)
    wireFluxAppToWidgets(fluxApp, child.get());
}

// ----------------------------------------------------------------------------
// getFluxApp  (private helper used throughout this file)
// Returns the FluxAppWidget if the root is one, otherwise nullptr.
// ----------------------------------------------------------------------------

static inline FluxAppWidget *getFluxApp(const WidgetPtr &root) {
  return dynamic_cast<FluxAppWidget *>(root.get());
}

// ============================================================================
// UNIFIED OVERLAY DISPATCHERS
//
// All overlay hit-testing goes through these three functions.
// They iterate the overlay stack in REVERSE zIndex order (highest first) so
// the topmost rendered widget always wins input.
//
// Adding a new overlay type (tooltip, context menu, etc.) requires ZERO
// changes here — as long as the widget registers itself via addOverlay and
// implements the relevant handleXxx virtual, it just works.
// ============================================================================

// ----------------------------------------------------------------------------
// handleOverlayMouseDown
// Called from WM_LBUTTONDOWN before the normal widget tree.
// Returns true if an overlay consumed the event.
// ----------------------------------------------------------------------------
inline bool FluxUI::handleDropdownOverlays(int mouseX, int mouseY) {
  FluxAppWidget *fluxApp = getFluxApp(root);
  if (!fluxApp || !fluxApp->hasOverlays())
    return false;

  const auto &stack = fluxApp->getOverlayStack();

  // Iterate highest zIndex first — topmost overlay wins
  for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
    if (it->widget && it->widget->handleMouseDown(mouseX, mouseY))
      return true;
  }

  return false;
}

// ----------------------------------------------------------------------------
// handleOverlayMouseMove
// Called from WM_MOUSEMOVE before the normal widget tree.
// Needed for tooltips (show on hover) and context menu hover highlight.
// Returns true if an overlay consumed the event.
// ----------------------------------------------------------------------------
// NOTE: reusing the "dialog overlays" slot for the unified mouse-move path.
// Both functions are called from WindowProc; we keep the names from the header
// to avoid changing flux_core.hpp's private declarations, but the
// implementation is now fully unified.
inline bool FluxUI::handleDialogOverlays(int mouseX, int mouseY) {
  // This function is intentionally left as a no-op here.
  // Mouse-move overlay routing is handled inside WM_MOUSEMOVE directly
  // via handleOverlayMouseMove (see WindowProc).
  // Keeping the symbol to satisfy the declaration in flux_core.hpp.
  (void)mouseX;
  (void)mouseY;
  return false;
}

// ----------------------------------------------------------------------------
// handleOverlayMouseMove  (new — called from WM_MOUSEMOVE in WindowProc)
// ----------------------------------------------------------------------------
inline bool FluxUI::handleOverlayMouseMove(int mouseX, int mouseY) {
  FluxAppWidget *fluxApp = getFluxApp(root);
  if (!fluxApp || !fluxApp->hasOverlays())
    return false;

  const auto &stack = fluxApp->getOverlayStack();

  for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
    if (it->widget && it->widget->handleMouseMove(mouseX, mouseY))
      return true;
  }

  return false;
}

// ----------------------------------------------------------------------------
// handleOverlayMouseWheel  (new — called from WM_MOUSEWHEEL in WindowProc)
// ----------------------------------------------------------------------------
inline bool FluxUI::handleOverlayMouseWheel(int delta) {
  FluxAppWidget *fluxApp = getFluxApp(root);
  if (!fluxApp || !fluxApp->hasOverlays())
    return false;

  const auto &stack = fluxApp->getOverlayStack();

  for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
    if (it->widget && it->widget->handleMouseWheel(delta))
      return true;
  }

  return false;
}

// ----------------------------------------------------------------------------
// handleOverlayKeyDown  (new — called from WM_KEYDOWN in WindowProc)
// Sends key events to the topmost overlay first (e.g. Escape closes a
// context menu before it reaches the focused text field behind it).
// ----------------------------------------------------------------------------
inline bool FluxUI::handleOverlayKeyDown(int keyCode) {
  FluxAppWidget *fluxApp = getFluxApp(root);
  if (!fluxApp || !fluxApp->hasOverlays())
    return false;

  // Only the topmost overlay receives keyboard events
  Widget *top = fluxApp->getTopmostOverlay();
  if (top)
    return top->handleKeyDown(keyCode);

  return false;
}

// ----------------------------------------------------------------------------
// checkDropdownOverlays / checkDialogOverlays
// These two were the old per-type tree-walk hit-testers.
// They are kept as empty stubs so flux_core.hpp compiles without changes.
// All real work now goes through handleDropdownOverlays (unified above).
// ----------------------------------------------------------------------------
inline bool FluxUI::checkDropdownOverlays(Widget *, int, int) { return false; }
inline bool FluxUI::checkDialogOverlays(Widget *, int, int) { return false; }

// ============================================================================
// REBUILD
// ============================================================================

inline void FluxUI::rebuild() {
  if (!builder)
    return;

  root = builder();

  // Wire overlay-capable widgets to the FluxAppWidget
  if (auto *fluxApp = dynamic_cast<FluxAppWidget *>(root.get()))
    wireFluxAppToWidgets(fluxApp, root.get());

  if (hwnd) {
    RECT rect;
    GetClientRect(hwnd, &rect);
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;

    HDC hdc = GetDC(hwnd);
    LayoutEngine::computeLayout(hdc, root.get(), w, h, fontCache);
    LayoutEngine::positionWidget(root.get(), 0, 0);
    ReleaseDC(hwnd, hdc);

    InvalidateRect(hwnd, NULL, FALSE);
  }
}

#endif // FLUX_CORE_IMPL_HPP