#ifndef FLUX_CORE_IMPL_HPP
#define FLUX_CORE_IMPL_HPP

#include "flux_app.hpp"
#include "flux_core.hpp"
#include "flux_overlay_host.hpp"          // ← only new include needed


// ============================================================================
// FLUXUI METHOD IMPLEMENTATIONS
// ============================================================================

template <typename T> inline State<T> FluxUI::useState(T initialValue) {
  return State<T>(initialValue, this);
}

// ----------------------------------------------------------------------------
// wireScaffoldToWidgets
//
// OLD: one dynamic_cast branch per concrete overlay type — every new overlay
//      widget required a matching line here.
//
// NEW: a single OverlayHost cast.  Any widget that inherits OverlayHost is
//      automatically wired; no changes to this function ever needed again.
// ----------------------------------------------------------------------------
inline void FluxUI::wireScaffoldToWidgets(ScaffoldWidget *scaffold,
                                          Widget *widget) {
  if (!widget) return;

  // If the widget opted in to the overlay system, wire it.
  if (auto *host = dynamic_cast<OverlayHost *>(widget))
    host->setScaffold(scaffold);

  // Always recurse — non-overlay widgets may have overlay-widget descendants.
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
// ----------------------------------------------------------------------------
static inline FluxAppWidget *getFluxApp(const WidgetPtr &root) {
  return dynamic_cast<FluxAppWidget *>(root.get());
}

// ============================================================================
// UNIFIED OVERLAY DISPATCHERS
// ============================================================================

inline bool FluxUI::handleDropdownOverlays(int mouseX, int mouseY) {
  auto *scaffold = findScaffold(root.get());
  if (!scaffold || !scaffold->hasOverlays()) return false;
  const auto &stack = scaffold->getOverlayStack();
  for (auto it = stack.rbegin(); it != stack.rend(); ++it)
    if (it->widget && it->widget->handleMouseDown(mouseX, mouseY)) return true;
  return false;
}

inline bool FluxUI::handleDialogOverlays(int mouseX, int mouseY) {
  (void)mouseX; (void)mouseY;
  return false;
}

inline bool FluxUI::handleOverlayMouseMove(int mouseX, int mouseY) {
  auto *scaffold = findScaffold(root.get());
  if (!scaffold || !scaffold->hasOverlays()) return false;
  const auto &stack = scaffold->getOverlayStack();
  for (auto it = stack.rbegin(); it != stack.rend(); ++it)
    if (it->widget && it->widget->handleMouseMove(mouseX, mouseY)) return true;
  return false;
}

inline bool FluxUI::handleOverlayMouseWheel(int delta) {
  auto *scaffold = findScaffold(root.get());
  if (!scaffold || !scaffold->hasOverlays()) return false;
  const auto &stack = scaffold->getOverlayStack();
  for (auto it = stack.rbegin(); it != stack.rend(); ++it)
    if (it->widget && it->widget->handleMouseWheel(delta)) return true;
  return false;
}

inline bool FluxUI::handleOverlayKeyDown(int keyCode) {
  auto *scaffold = findScaffold(root.get());
  if (!scaffold || !scaffold->hasOverlays()) return false;
  Widget *top = scaffold->getTopmostOverlay();
  return top ? top->handleKeyDown(keyCode) : false;
}

inline bool FluxUI::handleOverlayRightClick(int mouseX, int mouseY) {
  auto *scaffold = findScaffold(root.get());
  if (!scaffold || !scaffold->hasOverlays()) return false;
  const auto &stack = scaffold->getOverlayStack();
  for (auto it = stack.rbegin(); it != stack.rend(); ++it)
    if (it->widget && it->widget->handleRightClick(mouseX, mouseY)) return true;
  return false;
}

inline bool FluxUI::checkDropdownOverlays(Widget *, int, int) { return false; }
inline bool FluxUI::checkDialogOverlays(Widget *, int, int)   { return false; }

// ============================================================================
// REBUILD
// ============================================================================

inline void FluxUI::rebuild() {
  if (!builder) return;

  if (root) {
    root->onDetach();
    if (auto *scaffold = findScaffold(root.get()))
      scaffold->clearOverlays();
  }

  root = builder();

  // Wire scaffold into ALL overlay-capable widgets via the OverlayHost mixin.
  // No manual registration needed for new widget types.
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