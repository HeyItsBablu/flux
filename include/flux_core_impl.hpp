#ifndef FLUX_CORE_IMPL_HPP
#define FLUX_CORE_IMPL_HPP


#include "flux_core.hpp"
#include "flux_app.hpp"
#include "widgets/flux_overlays.hpp"


// ============================================================================
// FLUXUI METHOD IMPLEMENTATIONS
// ============================================================================


template <typename T>
inline State<T> FluxUI::useState(T initialValue) {
    return State<T>(initialValue, this);
}

// ----------------------------------------------------------------------------
// wireFluxAppToWidgets
// Walk the entire widget tree and hand every overlay-capable widget a pointer
// to the FluxAppWidget so they can call addOverlay / removeOverlay.
// Add new widget types here as they are introduced.
// ----------------------------------------------------------------------------

inline void FluxUI::wireFluxAppToWidgets(FluxAppWidget *fluxApp,
                                         Widget *widget) {
  if (!widget)
    return;

  if (auto *dropdown = dynamic_cast<DropdownWidget *>(widget))
    dropdown->setFluxApp(fluxApp);

  if (auto *dialog = dynamic_cast<DialogWidget *>(widget))
    dialog->setFluxApp(fluxApp);

  if (auto *tooltip = dynamic_cast<TooltipWidget *>(widget))
    tooltip->setFluxApp(fluxApp);

  if (auto *ctxMenu = dynamic_cast<ContextMenuWidget *>(widget))
    ctxMenu->setFluxApp(fluxApp);

  // Future overlay-capable widgets go here

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
// ============================================================================

// ----------------------------------------------------------------------------
// handleDropdownOverlays — unified mouse-down dispatcher for ALL overlays.
// Iterates highest zIndex first so topmost overlay wins.
// ----------------------------------------------------------------------------
inline bool FluxUI::handleDropdownOverlays(int mouseX, int mouseY) {
  FluxAppWidget *fluxApp = getFluxApp(root);
  if (!fluxApp || !fluxApp->hasOverlays())
    return false;

  const auto &stack = fluxApp->getOverlayStack();

  for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
    if (it->widget && it->widget->handleMouseDown(mouseX, mouseY))
      return true;
  }

  return false;
}

// ----------------------------------------------------------------------------
// handleDialogOverlays — no-op stub kept for source compatibility.
// All overlay mouse-down routing goes through handleDropdownOverlays above.
// ----------------------------------------------------------------------------
inline bool FluxUI::handleDialogOverlays(int mouseX, int mouseY) {
  (void)mouseX;
  (void)mouseY;
  return false;
}

// ----------------------------------------------------------------------------
// handleOverlayMouseMove — called from WM_MOUSEMOVE.
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
// handleOverlayMouseWheel — called from WM_MOUSEWHEEL.
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
// handleOverlayKeyDown — called from WM_KEYDOWN.
// Only the topmost overlay receives keyboard events (e.g. Escape closes a
// context menu before it reaches the text field behind it).
// ----------------------------------------------------------------------------
inline bool FluxUI::handleOverlayKeyDown(int keyCode) {
  FluxAppWidget *fluxApp = getFluxApp(root);
  if (!fluxApp || !fluxApp->hasOverlays())
    return false;

  Widget *top = fluxApp->getTopmostOverlay();
  if (top)
    return top->handleKeyDown(keyCode);

  return false;
}

// ----------------------------------------------------------------------------
// handleOverlayRightClick — called from WM_RBUTTONDOWN.
// Right-click on an open overlay (context menu, dropdown, etc.) should close it.
// ----------------------------------------------------------------------------
inline bool FluxUI::handleOverlayRightClick(int mouseX, int mouseY) {
  FluxAppWidget *fluxApp = getFluxApp(root);
  if (!fluxApp || !fluxApp->hasOverlays())
    return false;

  const auto &stack = fluxApp->getOverlayStack();

  for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
    if (it->widget && it->widget->handleRightClick(mouseX, mouseY))
      return true;
  }

  return false;
}

// ----------------------------------------------------------------------------
// checkDropdownOverlays / checkDialogOverlays
// Legacy empty stubs — all real work goes through handleDropdownOverlays.
// ----------------------------------------------------------------------------
inline bool FluxUI::checkDropdownOverlays(Widget *, int, int) { return false; }
inline bool FluxUI::checkDialogOverlays(Widget *, int, int) { return false; }

// ============================================================================
// REBUILD
// ============================================================================

inline void FluxUI::rebuild() {
  if (!builder)
    return;

  // ── Fix: detach the old tree before dropping it ─────────────────────────
  // onDetach() propagates through the entire widget tree so every overlay-
  // capable widget (Dropdown, Tooltip, Dialog, ContextMenu, …) calls
  // removeOverlay on itself.  This guarantees the FluxAppWidget's overlay
  // stack is empty and contains no dangling pointers before we build the new
  // tree.
  if (root) {
    root->onDetach();

    // If root is a FluxAppWidget, belt-and-suspenders clear of the stack.
    // onDetach() should have already emptied it, but this guards against any
    // widget that forgot to call fluxApp->removeOverlay in its onDetach().
    if (auto *fluxApp = dynamic_cast<FluxAppWidget *>(root.get()))
      fluxApp->clearOverlays();
  }
  // ────────────────────────────────────────────────────────────────────────

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