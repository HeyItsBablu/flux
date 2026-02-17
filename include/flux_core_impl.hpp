#ifndef FLUX_CORE_IMPL_HPP
#define FLUX_CORE_IMPL_HPP

#include "flux_app.hpp"
#include "flux_core.hpp"
#include "flux_dropdown.hpp"
#include "flux_dialog.hpp"

// ============================================================================
// FLUXUI METHOD IMPLEMENTATIONS (Need full class definitions)
// ============================================================================

inline void FluxUI::wireFluxAppToWidgets(FluxAppWidget *fluxApp,
                                         Widget *widget) {
  if (!widget)
    return;

  // Wire dropdown to FluxApp
  if (auto *dropdown = dynamic_cast<DropdownWidget *>(widget)) {
    dropdown->setFluxApp(fluxApp);
  }

  if (auto *dialog = dynamic_cast<DialogWidget *>(widget)) {
    dialog->setFluxApp(fluxApp);
  }

  // Recursively wire children
  for (auto &child : widget->children) {
    wireFluxAppToWidgets(fluxApp, child.get());
  }
}

inline bool FluxUI::checkDropdownOverlays(Widget *widget, int mouseX,
                                          int mouseY) {
  if (!widget)
    return false;

  // Check children first (they render on top)
  for (auto it = widget->children.rbegin(); it != widget->children.rend();
       ++it) {
    if (checkDropdownOverlays(it->get(), mouseX, mouseY))
      return true;
  }


  // Check if this is an open dropdown
  if (auto *dropdown = dynamic_cast<DropdownWidget *>(widget)) {
    if (dropdown->isOpen) {
      // Let the dropdown handle the click (it will check if click is in
      // overlay bounds)
      if (dropdown->handleMouseDown(mouseX, mouseY))
        return true;
    }
  }

  return false;
}

inline bool FluxUI::checkDialogOverlays(Widget *widget, int mouseX,
                                          int mouseY) {
  if (!widget)
    return false;

  // Check children first (they render on top)
  for (auto it = widget->children.rbegin(); it != widget->children.rend();
       ++it) {
    if (checkDialogOverlays(it->get(), mouseX, mouseY))
      return true;
  }

  //Check if this is an open dialog
  if (auto *dialog = dynamic_cast<DialogWidget *>(widget)) {
    if (dialog->isOpen) {
      if (dialog->handleMouseDown(mouseX, mouseY))
        return true;
    }
  }


  return false;
}

inline void FluxUI::rebuild() {
  if (!builder)
    return;

  root = builder();

  // 🎯 Auto-wire FluxApp to all dropdowns
  if (auto *fluxApp = dynamic_cast<FluxAppWidget *>(root.get())) {
    wireFluxAppToWidgets(fluxApp, root.get());
  }

  if (hwnd) {
    RECT rect;
    GetClientRect(hwnd, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    HDC hdc = GetDC(hwnd);
    LayoutEngine::computeLayout(hdc, root.get(), width, height, fontCache);
    LayoutEngine::positionWidget(root.get(), 0, 0);
    ReleaseDC(hwnd, hdc);

    InvalidateRect(hwnd, NULL, FALSE);
  }
}

#endif // FLUX_CORE_IMPL_HPP