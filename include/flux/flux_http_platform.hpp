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
    auto* p = reinterpret_cast<FluxHttpPayload*>(lParam);
    if (p && p->callback) p->callback(std::move(p->result));
    delete p;
}

static HWND gFluxUIWindow = nullptr;
inline void fluxSetUIWindow(HWND hwnd) { gFluxUIWindow = hwnd; }

inline void fluxPostToUIThread(HttpCallback callback, HttpResult result) {
    if (!gFluxUIWindow) { if (callback) callback(result); return; }
    auto* p = new FluxHttpPayload{ std::move(callback), std::move(result) };
    PostMessage(gFluxUIWindow, WM_FLUX_HTTP_RESULT, 0, (LPARAM)p);
}

// flux_http_platform.hpp — Linux section

#elif defined(__linux__) && !defined(__ANDROID__)
#include <SDL2/SDL.h>
#include <mutex>
#include <vector>

struct FluxHttpPayload {
    HttpCallback callback;
    HttpResult   result;
};

static Uint32             gFluxHttpEventType = (Uint32)-1;
static std::mutex         gFluxQueueMutex;
static std::vector<FluxHttpPayload*> gFluxQueue;

inline void fluxInitUIThread() {
    gFluxHttpEventType = SDL_RegisterEvents(1);
}

inline void fluxPostToUIThread(HttpCallback callback, HttpResult result) {
    if (gFluxHttpEventType == (Uint32)-1) {
        if (callback) callback(std::move(result));
        return;
    }
    auto* p = new FluxHttpPayload{ std::move(callback), std::move(result) };
    {
        std::lock_guard<std::mutex> lock(gFluxQueueMutex);
        gFluxQueue.push_back(p);
    }
    SDL_Event e{};
    e.type = gFluxHttpEventType;
    SDL_PushEvent(&e);
}

// Drains into a local snapshot so the mutex is not held during callbacks
inline void fluxProcessHttpEvents() {
    std::vector<FluxHttpPayload*> local;
    {
        std::lock_guard<std::mutex> lock(gFluxQueueMutex);
        local.swap(gFluxQueue);   // move all pending into local, queue is now empty
    }
    for (auto* p : local) {
        if (p) {
            if (p->callback) p->callback(std::move(p->result));
            delete p;
        }
    }
}





#elif defined(__ANDROID__)
// ─── Android / ALooper ──────────────────────────────────────────────────────


#include <android/looper.h>
#include <unistd.h>

struct FluxHttpPayload {
    HttpCallback callback;
    HttpResult   result;
};

static int  gPipeFd[2]    = {-1, -1};
static ALooper* gLooper   = nullptr;

inline void fluxAndroidInitLooper() {
    pipe(gPipeFd);
    gLooper = ALooper_forThread();
    ALooper_addFd(gLooper, gPipeFd[0], 0, ALOOPER_EVENT_INPUT,
        [](int fd, int, void*) -> int {
            FluxHttpPayload* p = nullptr;
            read(fd, &p, sizeof(p));
            if (p) {
                if (p->callback) p->callback(std::move(p->result));
                delete p;
            }
            return 1;   // keep listening
        }, nullptr);
}

inline void fluxPostToUIThread(HttpCallback callback, HttpResult result) {
    if (gPipeFd[1] < 0) { if (callback) callback(result); return; }
    auto* p = new FluxHttpPayload{ std::move(callback), std::move(result) };
    write(gPipeFd[1], &p, sizeof(p));
}

#endif