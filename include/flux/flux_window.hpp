#ifndef FLUX_WINDOW_HPP
#define FLUX_WINDOW_HPP

#include "flux_font.hpp"
#include "flux_platform.hpp"

#include <functional>
#include <string>

struct WindowCallbacks {
  std::function<void(GraphicsContext &ctx, int width, int height)> onPaint;
  std::function<void(GraphicsContext &ctx, int width, int height)> onResize;

  std::function<bool(int x, int y)> onMouseDown;
  std::function<bool(int x, int y)> onMouseUp;
  std::function<bool(int x, int y)> onMouseMove;
  std::function<bool(int x, int y)> onRightClick;
  std::function<bool(int delta)> onMouseWheel;
  std::function<void()> onMouseLeave;

  std::function<bool(int keyCode)> onKeyDown;
  std::function<bool(wchar_t ch)> onChar;

  // TimerID is already a cross-platform alias defined in flux_platform.hpp
  std::function<void(TimerID timerId)> onTimer;
};

class PlatformWindow {
public:
  PlatformWindow() = default;
  ~PlatformWindow() { destroy(); }

  PlatformWindow(const PlatformWindow &) = delete;
  PlatformWindow &operator=(const PlatformWindow &) = delete;

  bool create(const std::string &title, int width, int height,
              AppInstance hInstance, void *userData);
  void destroy();
  int run();

  WindowCallbacks callbacks;

  void invalidate();
  void invalidateRect(int x, int y, int w, int h);
  // Use cross-platform TimerID alias instead of UINT
  void setTimer(TimerID id, int ms);
  void killTimer(TimerID id);

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

  void startupGdiplus();
  GraphicsContext getMeasureContext() const;

private:
  NativeWindow hwnd = nullptr;
  AppInstance hInstance = nullptr;
  int cachedWidth = 0;
  int cachedHeight = 0;

#ifdef _WIN32
  BackBuffer backBuffer;
  ULONG_PTR gdiplusToken = 0;

  void shutdownGdiplus();
  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam,
                                     LPARAM lParam);
#endif
#ifndef _WIN32
  SDL_Window *nativeHandle = nullptr;
  bool running = false;
  bool mouseCapture = false;
  bool dirty = false;
  CairoState *cairoState = nullptr;
  std::unordered_map<TimerID, SDL_TimerID> sdlTimerMap;

  void handleSDLEvent(const SDL_Event &);
  NativeWindow handle() const;

#endif

  void updateClientSize();
};

#endif // FLUX_WINDOW_HPP