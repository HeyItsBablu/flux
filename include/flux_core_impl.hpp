#ifndef FLUX_CORE_IMPL_HPP
#define FLUX_CORE_IMPL_HPP

#include "flux_app.hpp"
#include "flux_core.hpp"
#include "widgets/flux_overlays.hpp"

// ============================================================================
// FLUXUI METHOD IMPLEMENTATIONS
// ============================================================================

template <typename T> inline State<T> FluxUI::useState(T initialValue) {
  return State<T>(initialValue, this);
}

// ----------------------------------------------------------------------------
// wireFluxAppToWidgets
// Walk the entire widget tree and hand every overlay-capable widget a pointer
// to the FluxAppWidget so they can call addOverlay / removeOverlay.
// Add new widget types here as they are introduced.
// ----------------------------------------------------------------------------

// wireScaffoldToWidgets — inject scaffold into every overlay-capable widget
inline void FluxUI::wireScaffoldToWidgets(ScaffoldWidget *scaffold, Widget *widget) {
  if (!widget) return;

  if (auto *d = dynamic_cast<DropdownWidget *>(widget))
    d->setScaffold(scaffold);
  if (auto *t = dynamic_cast<TooltipWidget *>(widget))
    t->setScaffold(scaffold);
  if (auto *c = dynamic_cast<ContextMenuWidget *>(widget))
    c->setScaffold(scaffold);
  if (auto *dlg = dynamic_cast<DialogWidget *>(widget))
    dlg->setScaffold(scaffold);

  for (auto &child : widget->children)
    wireScaffoldToWidgets(scaffold, child.get());
}

// Helper: find ScaffoldWidget anywhere in the tree (depth-first)
static inline ScaffoldWidget *findScaffold(Widget *widget) {
  if (!widget) return nullptr;
  if (auto *s = dynamic_cast<ScaffoldWidget *>(widget)) return s;
  for (auto &child : widget->children) {
    if (auto *s = findScaffold(child.get())) return s;
  }
  return nullptr;
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
  auto *scaffold = findScaffold(root.get());
  if (!scaffold || !scaffold->hasOverlays()) return false;
  const auto &stack = scaffold->getOverlayStack();
  for (auto it = stack.rbegin(); it != stack.rend(); ++it)
    if (it->widget && it->widget->handleMouseDown(mouseX, mouseY)) return true;
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
  auto *scaffold = findScaffold(root.get());
  if (!scaffold || !scaffold->hasOverlays()) return false;
  const auto &stack = scaffold->getOverlayStack();
  for (auto it = stack.rbegin(); it != stack.rend(); ++it)
    if (it->widget && it->widget->handleMouseMove(mouseX, mouseY)) return true;
  return false;
}
// ----------------------------------------------------------------------------
// handleOverlayMouseWheel — called from WM_MOUSEWHEEL.
// ----------------------------------------------------------------------------
inline bool FluxUI::handleOverlayMouseWheel(int delta) {
  auto *scaffold = findScaffold(root.get());
  if (!scaffold || !scaffold->hasOverlays()) return false;
  const auto &stack = scaffold->getOverlayStack();
  for (auto it = stack.rbegin(); it != stack.rend(); ++it)
    if (it->widget && it->widget->handleMouseWheel(delta)) return true;
  return false;
}

// ----------------------------------------------------------------------------
// handleOverlayKeyDown — called from WM_KEYDOWN.
// Only the topmost overlay receives keyboard events (e.g. Escape closes a
// context menu before it reaches the text field behind it).
// ----------------------------------------------------------------------------
inline bool FluxUI::handleOverlayKeyDown(int keyCode) {
  auto *scaffold = findScaffold(root.get());
  if (!scaffold || !scaffold->hasOverlays()) return false;
  Widget *top = scaffold->getTopmostOverlay();
  return top ? top->handleKeyDown(keyCode) : false;
}

// ----------------------------------------------------------------------------
// handleOverlayRightClick — called from WM_RBUTTONDOWN.
// Right-click on an open overlay (context menu, dropdown, etc.) should close
// it.
// ----------------------------------------------------------------------------
inline bool FluxUI::handleOverlayRightClick(int mouseX, int mouseY) {
  auto *scaffold = findScaffold(root.get());
  if (!scaffold || !scaffold->hasOverlays()) return false;
  const auto &stack = scaffold->getOverlayStack();
  for (auto it = stack.rbegin(); it != stack.rend(); ++it)
    if (it->widget && it->widget->handleRightClick(mouseX, mouseY)) return true;
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

// rebuild
inline void FluxUI::rebuild() {
  if (!builder) return;

  if (root) {
    root->onDetach();
    // Belt-and-suspenders: clear scaffold overlays if still populated
    if (auto *scaffold = findScaffold(root.get()))
      scaffold->clearOverlays();
  }

  root = builder();

  // Wire scaffold into all overlay-capable widgets
  if (auto *scaffold = findScaffold(root.get()))
    wireScaffoldToWidgets(scaffold, root.get());

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