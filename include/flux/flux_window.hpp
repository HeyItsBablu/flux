#ifndef FLUX_WINDOW_HPP
#define FLUX_WINDOW_HPP

#include "flux_platform.hpp"
#include "flux_font.hpp"

#include <functional>
#include <string>

// flux_window.hpp


// ============================================================================
// PLATFORM WINDOW — owns the OS window, message loop, and back-buffer
// ============================================================================

struct WindowCallbacks {
  // Called when the window needs a full repaint
  std::function<void(GraphicsContext &ctx, int width, int height)> onPaint;

  // Called when the window is resized
  std::function<void(GraphicsContext &ctx, int width, int height)> onResize;

  // Mouse events
  std::function<bool(int x, int y)> onMouseDown;
  std::function<bool(int x, int y)> onMouseUp;
  std::function<bool(int x, int y)> onMouseMove;
  std::function<bool(int x, int y)> onRightClick;
  std::function<bool(int delta)> onMouseWheel;
  std::function<void()> onMouseLeave;

  // Keyboard events
  std::function<bool(int keyCode)> onKeyDown;
  std::function<bool(wchar_t ch)> onChar;

  // Timer
  std::function<void(UINT timerId)> onTimer;
};

class PlatformWindow {
public:
  PlatformWindow() = default;
  ~PlatformWindow() { destroy(); }

  // No copy
  PlatformWindow(const PlatformWindow &) = delete;
  PlatformWindow &operator=(const PlatformWindow &) = delete;

  // ----------------------------------------------------------------
  // Lifecycle
  // ----------------------------------------------------------------
  bool create(const std::string &title, int width, int height,
              AppInstance hInstance, void *userData);
  void destroy();
  int run();

  // ----------------------------------------------------------------
  // Callbacks — set before create()
  // ----------------------------------------------------------------
  WindowCallbacks callbacks;

  // ----------------------------------------------------------------
  // Operations
  // ----------------------------------------------------------------
  void invalidate(); // whole window
  void invalidateRect(int x, int y, int w, int h);
  void setTimer(UINT id, int ms);
  void killTimer(UINT id);

  // ----------------------------------------------------------------
  // Accessors
  // ----------------------------------------------------------------
  NativeWindow handle() const { return hwnd; }
  int clientWidth() const { return cachedWidth; }
  int clientHeight() const { return cachedHeight; }
  bool valid() const { return hwnd != nullptr; }

  void setClipboardText(const std::string &text);
  std::string getClipboardText();

  void captureMouseInput();
  void releaseMouseInput();

  struct ScreenPoint {
    int x = 0, y = 0;
  };
  struct ClientSize {
    int width = 0, height = 0;
  };

  ScreenPoint clientToScreen(int cx, int cy) const;
  ScreenPoint screenToClient(int sx, int sy) const;
  ClientSize getClientSize() const;

  void setResizeCursorH();
void setResizeCursorV();
void setDefaultCursor();

private:
#ifdef _WIN32
  NativeWindow hwnd = nullptr;
  AppInstance hInstance = nullptr;
  BackBuffer backBuffer;
  int cachedWidth = 0;
  int cachedHeight = 0;
  ULONG_PTR gdiplusToken = 0;

  void startupGdiplus();
  void shutdownGdiplus();
  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam,
                                     LPARAM lParam);
  void updateClientSize();
#endif
};

#endif // FLUX_WINDOW_HPP