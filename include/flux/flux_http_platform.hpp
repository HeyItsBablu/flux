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

#elif defined(__linux__) && !defined(__ANDROID__)



#include <glib.h>

struct FluxHttpPayload {
    HttpCallback callback;
    HttpResult   result;
};

inline void fluxPostToUIThread(HttpCallback callback, HttpResult result) {
    auto* p = new FluxHttpPayload{ std::move(callback), std::move(result) };
    g_idle_add([](gpointer data) -> gboolean {
        auto* p = static_cast<FluxHttpPayload*>(data);
        if (p->callback) p->callback(std::move(p->result));
        delete p;
        return G_SOURCE_REMOVE;   // run once
    }, p);
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