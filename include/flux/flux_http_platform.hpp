#pragma once
#include "flux_http.hpp"

// ============================================================================
// fluxPostToUIThread — routes the HTTP result back to the UI thread.
// ============================================================================

#if defined(_WIN32)
// ─── Win32 ──────────────────────────────────────────────────────────────────

#include <windows.h>
static constexpr UINT WM_FLUX_HTTP_RESULT = WM_USER + 0x200;

struct FluxHttpPayload {
  HttpCallback callback;
  HttpResult   result;
};

inline void FluxHttp_Win32_HandleMessage(LPARAM lParam) {
  auto *p = reinterpret_cast<FluxHttpPayload *>(lParam);
  if (p) {
    if (p->callback) p->callback(std::move(p->result));
    delete p;
  }
}

static HWND gFluxUIWindow = nullptr;
inline void fluxSetUIWindow(HWND hwnd) { gFluxUIWindow = hwnd; }

inline void fluxDrainPendingMessages(HWND hwnd) {
  MSG msg;
  while (PeekMessage(&msg, hwnd, WM_FLUX_HTTP_RESULT, WM_FLUX_HTTP_RESULT, PM_REMOVE)) {
    auto *p = reinterpret_cast<FluxHttpPayload *>(msg.lParam);
    delete p;
  }
}

inline void fluxPostToUIThread(HttpCallback callback, HttpResult result) {
  if (!gFluxUIWindow) {
    if (callback) callback(std::move(result));
    return;
  }
  auto *p = new FluxHttpPayload{std::move(callback), std::move(result)};
  if (!PostMessage(gFluxUIWindow, WM_FLUX_HTTP_RESULT, 0, reinterpret_cast<LPARAM>(p)))
    delete p;
}

// ─── Linux / SDL2 ───────────────────────────────────────────────────────────

#elif defined(__linux__) && !defined(__ANDROID__)
#include <SDL2/SDL.h>
#include <cassert>
#include <mutex>
#include <vector>

struct FluxHttpPayload {
  HttpCallback callback;
  HttpResult   result;
};

static Uint32 gFluxHttpEventType = static_cast<Uint32>(-1);
static std::mutex gFluxQueueMutex;
static std::vector<FluxHttpPayload *> gFluxQueue;

// Call once after SDL_Init.
inline void fluxInitUIThread() {
  gFluxHttpEventType = SDL_RegisterEvents(1);
  assert(gFluxHttpEventType != static_cast<Uint32>(-1) &&
         "fluxInitUIThread: SDL_RegisterEvents failed. "
         "Make sure SDL_Init() is called before fluxInitUIThread().");
}

inline void fluxPostToUIThread(HttpCallback callback, HttpResult result) {
  if (gFluxHttpEventType == static_cast<Uint32>(-1)) {
    if (callback) callback(std::move(result));
    return;
  }

  // Enqueue the payload under the lock.
  auto *p = new FluxHttpPayload{std::move(callback), std::move(result)};
  {
    std::lock_guard<std::mutex> lock(gFluxQueueMutex);
    gFluxQueue.push_back(p);
  }


  SDL_Event e{};
  e.type = gFluxHttpEventType;
  if (SDL_PushEvent(&e) < 0) {

    std::lock_guard<std::mutex> lock(gFluxQueueMutex);
    auto it = std::find(gFluxQueue.begin(), gFluxQueue.end(), p);
    if (it != gFluxQueue.end()) gFluxQueue.erase(it);
    delete p;
  }
}


inline void fluxProcessHttpEvents() {
  std::vector<FluxHttpPayload *> local;
  {
    std::lock_guard<std::mutex> lock(gFluxQueueMutex);
    local.swap(gFluxQueue);
  }
  for (auto *p : local) {
    if (p) {
      if (p->callback) p->callback(std::move(p->result));
      delete p;
    }
  }
}


inline void fluxShutdownHttpQueue() {
  std::lock_guard<std::mutex> lock(gFluxQueueMutex);
  for (auto *p : gFluxQueue) delete p;
  gFluxQueue.clear();
}

// ─── Android / ALooper ──────────────────────────────────────────────────────

#elif defined(__ANDROID__)
#include <android/looper.h>
#include <cassert>
#include <unistd.h>

struct FluxHttpPayload {
  HttpCallback callback;
  HttpResult   result;
};

static int     gPipeFd[2] = {-1, -1};
static ALooper *gLooper   = nullptr;

inline void fluxAndroidInitLooper() {
  int rc = pipe(gPipeFd);
  assert(rc == 0 && "fluxAndroidInitLooper: pipe() failed");

  gLooper = ALooper_forThread();
  assert(gLooper && "fluxAndroidInitLooper: must be called from the UI thread");

  ALooper_addFd(
      gLooper, gPipeFd[0], 0, ALOOPER_EVENT_INPUT,
      [](int fd, int, void *) -> int {
        FluxHttpPayload *p = nullptr;
        while (read(fd, &p, sizeof(p)) == sizeof(p)) {
          if (p) {
            if (p->callback) p->callback(std::move(p->result));
            delete p;
            p = nullptr;
          }
        }
        return 1;
      },
      nullptr);
}

inline void fluxPostToUIThread(HttpCallback callback, HttpResult result) {
  if (gPipeFd[1] < 0) {
    if (callback) callback(std::move(result));
    return;
  }
  auto *p = new FluxHttpPayload{std::move(callback), std::move(result)};
  if (write(gPipeFd[1], &p, sizeof(p)) != sizeof(p))
    delete p;
}

inline void fluxAndroidShutdownLooper() {
  if (gLooper && gPipeFd[0] >= 0)
    ALooper_removeFd(gLooper, gPipeFd[0]);
  if (gPipeFd[0] >= 0) { close(gPipeFd[0]); gPipeFd[0] = -1; }
  if (gPipeFd[1] >= 0) { close(gPipeFd[1]); gPipeFd[1] = -1; }
  gLooper = nullptr;
}

#endif