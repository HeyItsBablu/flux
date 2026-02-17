#ifndef FLUX_DIALOG_HPP
#define FLUX_DIALOG_HPP

#include "flux_app.hpp"
#include "flux_core.hpp"
#include <functional>

// ============================================================================
// DIALOG WIDGET
// ============================================================================

class DialogWidget : public Widget {
private:
  FluxAppWidget *fluxApp = nullptr;
  bool contentLayoutDirty = true; // Track whether layout needs to run

public:
  bool isOpen = false;
  WidgetPtr content;

  // Dialog styling
  int dialogWidth = 400;
  int dialogHeight = 300;
  COLORREF overlayColor = RGB(0, 0, 0); // Black overlay
  int overlayAlpha = 128;               // 50% opacity (0-255)
  COLORREF dialogBgColor = RGB(255, 255, 255);
  COLORREF dialogBorderColor = RGB(200, 200, 200);
  int dialogBorderRadius = 8;
  int dialogPadding = 24;

  // Callbacks
  std::function<void()> onClose;
  bool closeOnClickOutside = true;

  DialogWidget() { hasBackground = false; }

  // ----------------------------------------------------------------
  // SET FLUX APP
  // ----------------------------------------------------------------
  void setFluxApp(FluxAppWidget *app) { fluxApp = app; }

  // ----------------------------------------------------------------
  // Layout
  // ----------------------------------------------------------------
  void computeLayout(HDC hdc, int availableWidth, int availableHeight,
                     FontCache &fontCache) override {
    // Dialog doesn't take up space in normal layout
    width = 0;
    height = 0;
    needsLayout = false;
  }

  void render(HDC hdc, FontCache &fontCache) override {
    // Dialog renders via overlay system, not normal render
    needsPaint = false;
  }

  // ----------------------------------------------------------------
  // Mouse Events
  // ----------------------------------------------------------------
  bool handleMouseDown(int mx, int my) override {
    if (!isOpen)
      return false;

    RECT windowRect = {0, 0, 800, 600};
    if (fluxApp) {
      windowRect.right = fluxApp->width;
      windowRect.bottom = fluxApp->height;
    }

    int dialogX = (windowRect.right - dialogWidth) / 2;
    int dialogY = (windowRect.bottom - dialogHeight) / 2;

    // Always consume clicks when dialog is open (nothing behind should get it)
    // Check if clicked OUTSIDE dialog first
    if (mx < dialogX || mx >= dialogX + dialogWidth || my < dialogY ||
        my >= dialogY + dialogHeight) {
      if (closeOnClickOutside) {
        close();
      }
      return true; // Always consume - block clicks behind dialog
    }

    // Clicked INSIDE dialog - dispatch to content
    if (content) {
      Widget *toFocus = nullptr;

      // Walk the content tree properly
      if (findAndHandleMouseEvent(content.get(), mx, my,
                                  [mx, my, &toFocus](Widget *w) {
                                    bool handled = w->handleMouseDown(mx, my);
                                    // Also trigger onClick for button-like
                                    // widgets
                                    if (!handled && w->onClick &&
                                        mx >= w->x && mx < w->x + w->width &&
                                        my >= w->y && my < w->y + w->height) {
                                      w->onClick();
                                      handled = true;
                                    }
                                    // Capture focusable widget at the moment it
                                    // claims the event
                                    if (handled && w->isFocusable) {
                                      toFocus = w;
                                    }
                                    return handled;
                                  })) {
        // Sync focus with FluxUI after dialog dispatches click
        if (toFocus && FluxUI::getCurrentInstance()) {
          FluxUI::getCurrentInstance()->setFocus(toFocus);
        }
        return true;
      }

      // Fallback: check onClick directly on widgets at click position
      Widget *clicked = findWidgetAt(content.get(), mx, my);
      if (clicked) {
        if (clicked->onClick) {
          clicked->onClick();
          return true;
        }
        if (clicked->isFocusable) {
          clicked->handleMouseDown(mx, my);
          if (FluxUI::getCurrentInstance()) {
            FluxUI::getCurrentInstance()->setFocus(clicked);
          }
          return true;
        }
      }
    }

    return true; // Consume click inside dialog even if nothing handled it
  }

  // ----------------------------------------------------------------
  // Open/Close Dialog
  // ----------------------------------------------------------------
  void open() {
    if (isOpen || !fluxApp)
      return;

    isOpen = true;
    contentLayoutDirty = true; // Force layout on first render after open

    // Run layout eagerly so hit-testing is valid before the first click
    if (content) {
      HWND hwnd =
          FluxUI::getCurrentInstance() ? FluxUI::getCurrentInstance()->getWindow() : nullptr;
      if (hwnd) {
        HDC hdc = GetDC(hwnd);
        FontCache &fontCache = FluxUI::getCurrentInstance()->getFontCache();

        int contentW = dialogWidth - dialogPadding * 2;
        int contentH = dialogHeight - dialogPadding * 2;
        content->computeLayout(hdc, contentW, contentH, fontCache);

        RECT windowRect = {0, 0, 800, 600};
        windowRect.right = fluxApp->width;
        windowRect.bottom = fluxApp->height;
        int dialogX = (windowRect.right - dialogWidth) / 2;
        int dialogY = (windowRect.bottom - dialogHeight) / 2;
        int contentX = dialogX + dialogPadding;
        int contentY = dialogY + dialogPadding;

        content->x = contentX;
        content->y = contentY;
        content->positionChildren(
            contentX + content->paddingLeft, contentY + content->paddingTop,
            content->width - content->paddingLeft - content->paddingRight,
            content->height - content->paddingTop - content->paddingBottom);

        ReleaseDC(hwnd, hdc);
        contentLayoutDirty = false;
      }
    }

    // Register overlay with FluxApp
    fluxApp->addOverlay(
        this,
        [this](HDC hdc, FontCache &fontCache) {
          this->renderDialog(hdc, fontCache);
        },
        200 // zIndex: higher than dropdown (100)
    );

    markNeedsPaint();
  }

  void close() {
    if (!isOpen || !fluxApp)
      return;

    isOpen = false;
    contentLayoutDirty = true; // Reset for next open

    // Clear FluxUI focus if a widget inside this dialog currently owns it
    if (FluxUI::getCurrentInstance()) {
      Widget *focused = FluxUI::getCurrentInstance()->getFocusedWidget();
      if (focused && isDescendantOf(focused, content.get())) {
        FluxUI::getCurrentInstance()->setFocus(nullptr);
      }
    }

    // Unregister overlay
    fluxApp->removeOverlay(this);

    if (onClose) {
      onClose();
    }

    markNeedsPaint();
  }

  // ----------------------------------------------------------------
  // Render Dialog (Called by FluxApp overlay system)
  // ----------------------------------------------------------------
  void renderDialog(HDC hdc, FontCache &fontCache) {
    if (!isOpen)
      return;

    RECT windowRect = {0, 0, 800, 600};
    if (fluxApp) {
      windowRect.right = fluxApp->width;
      windowRect.bottom = fluxApp->height;
    }

    // Draw semi-transparent overlay
    HBRUSH overlayBrush = CreateSolidBrush(overlayColor);
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, (BYTE)overlayAlpha, 0};

    HDC hdcOverlay = CreateCompatibleDC(hdc);
    HBITMAP hbmOverlay =
        CreateCompatibleBitmap(hdc, windowRect.right, windowRect.bottom);
    HBITMAP hbmOldOverlay = (HBITMAP)SelectObject(hdcOverlay, hbmOverlay);

    FillRect(hdcOverlay, &windowRect, overlayBrush);

    AlphaBlend(hdc, 0, 0, windowRect.right, windowRect.bottom, hdcOverlay, 0,
               0, windowRect.right, windowRect.bottom, blend);

    SelectObject(hdcOverlay, hbmOldOverlay);
    DeleteObject(hbmOverlay);
    DeleteDC(hdcOverlay);
    DeleteObject(overlayBrush);

    int dialogX = (windowRect.right - dialogWidth) / 2;
    int dialogY = (windowRect.bottom - dialogHeight) / 2;

    // Draw dialog background
    HBRUSH dialogBrush = CreateSolidBrush(dialogBgColor);
    HPEN dialogPen = CreatePen(PS_SOLID, 1, dialogBorderColor);

    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, dialogBrush);
    HPEN oldPen = (HPEN)SelectObject(hdc, dialogPen);

    RoundRect(hdc, dialogX, dialogY, dialogX + dialogWidth,
              dialogY + dialogHeight, dialogBorderRadius * 2,
              dialogBorderRadius * 2);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(dialogBrush);
    DeleteObject(dialogPen);

    // Render content
    if (content) {
      int contentX = dialogX + dialogPadding;
      int contentY = dialogY + dialogPadding;
      int contentW = dialogWidth - dialogPadding * 2;
      int contentH = dialogHeight - dialogPadding * 2;

      // Only re-layout if dirty (e.g. content changed), not every frame
      if (contentLayoutDirty) {
        content->computeLayout(hdc, contentW, contentH, fontCache);

        content->x = contentX;
        content->y = contentY;
        content->positionChildren(
            contentX + content->paddingLeft, contentY + content->paddingTop,
            content->width - content->paddingLeft - content->paddingRight,
            content->height - content->paddingTop - content->paddingBottom);

        contentLayoutDirty = false;
      }

      content->render(hdc, fontCache);
    }
  }

  // ----------------------------------------------------------------
  // Builder Methods
  // ----------------------------------------------------------------
  std::shared_ptr<DialogWidget> setContent(WidgetPtr child) {
    content = child;
    if (content) {
      content->parent = this;
    }
    contentLayoutDirty = true; // Content changed, layout must re-run
    markNeedsPaint();
    return std::static_pointer_cast<DialogWidget>(shared_from_this());
  }

  std::shared_ptr<DialogWidget> setSize(int w, int h) {
    dialogWidth = w;
    dialogHeight = h;
    contentLayoutDirty = true;
    markNeedsPaint();
    return std::static_pointer_cast<DialogWidget>(shared_from_this());
  }

  std::shared_ptr<DialogWidget> setCloseOnClickOutside(bool value) {
    closeOnClickOutside = value;
    return std::static_pointer_cast<DialogWidget>(shared_from_this());
  }

  std::shared_ptr<DialogWidget> setOnClose(std::function<void()> callback) {
    onClose = callback;
    return std::static_pointer_cast<DialogWidget>(shared_from_this());
  }

  std::shared_ptr<DialogWidget> setOverlayAlpha(int alpha) {
    overlayAlpha = alpha; // 0-255
    return std::static_pointer_cast<DialogWidget>(shared_from_this());
  }

private:
  // ----------------------------------------------------------------
  // Helper: check whether a widget is a descendant of a subtree root
  // Used by close() to clear focus when the focused widget is inside
  // this dialog's content tree.
  // ----------------------------------------------------------------
  bool isDescendantOf(Widget *candidate, Widget *subtreeRoot) {
    if (!candidate || !subtreeRoot)
      return false;
    Widget *current = candidate->parent;
    while (current) {
      if (current == subtreeRoot)
        return true;
      current = current->parent;
    }
    // Also handle the case where candidate IS the subtree root
    return candidate == subtreeRoot;
  }
};

using DialogWidgetPtr = std::shared_ptr<DialogWidget>;

inline DialogWidgetPtr Dialog(WidgetPtr content = nullptr) {
  auto w = std::make_shared<DialogWidget>();
  if (content)
    w->setContent(content);
  return w;
}

#endif // FLUX_DIALOG_HPP