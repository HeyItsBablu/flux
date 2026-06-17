// flux_http_web.cpp
// Emscripten-only.  Compiled instead of (not alongside) the libcurl path.
// CMake already sets -sFETCH=1 and filters this file via:
//   list(FILTER FLUX_SOURCES EXCLUDE REGEX ".*_web\\.cpp$")  — on non-web
// So this file is only compiled when EMSCRIPTEN is defined.

#ifdef __EMSCRIPTEN__

#include "flux/flux_http.hpp"

#include <emscripten/fetch.h>

#include <atomic>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

// ============================================================================
// UI-THREAD CALLBACK QUEUE
//
// Emscripten Fetch callbacks fire on the main thread already (when
// EMSCRIPTEN_FETCH_SYNCHRONOUS is NOT set), so we don't need a pipe or
// SDL event — we just push into a queue and drain it from the main loop
// via fluxDrainHttpQueue() or from requestAnimationFrame.
//
// However, to keep the interface identical to the native platforms we still
// go through fluxPostToUIThread() / fluxDrainHttpQueue() so that callers
// never need to know which platform they're on.
// ============================================================================

namespace
{

    struct WebPendingCallback
    {
        HttpCallback callback;
        HttpResult result;
    };

    std::mutex gQueueMutex;
    std::queue<WebPendingCallback> gQueue;

} // namespace

// ── fluxPostToUIThread ───────────────────────────────────────────────────────
// Called from the Fetch completion callback (already main thread on web).
// We queue instead of calling directly so that callers can't accidentally
// nest re-entrant layout/render passes.
void fluxPostToUIThread(HttpCallback callback, HttpResult result)
{
  
    std::lock_guard<std::mutex> lock(gQueueMutex);
    gQueue.push({std::move(callback), std::move(result)});
}

// ── fluxDrainHttpQueue ───────────────────────────────────────────────────────
// Call once per frame from your main loop (e.g. inside the
// requestAnimationFrame callback or emscripten_set_main_loop body).
void fluxDrainHttpQueue()
{

    std::queue<WebPendingCallback> local;
    {
        std::lock_guard<std::mutex> lock(gQueueMutex);
        std::swap(local, gQueue);
    }
    while (!local.empty())
    {
        auto &item = local.front();
        if (item.callback)
            item.callback(std::move(item.result));
        local.pop();
    }
}

// ── No-ops matching the native platform API surface ──────────────────────────
// These are called from platform-agnostic startup / teardown code.

void fluxSetUIWindow(void * /*hwnd*/) {} // no HWND on web
void fluxDrainPendingMessages() {}       // superseded by fluxDrainHttpQueue

// ============================================================================
// PER-REQUEST STATE
// One heap-allocated block per in-flight request; freed in the callbacks.
// ============================================================================

struct WebFetchState
{
    HttpCallback callback;
    std::string requestBody; // kept alive for the duration of the fetch
};

// ============================================================================
// FETCH CALLBACKS (fire on main thread)
// ============================================================================

static void onFetchSuccess(emscripten_fetch_t *fetch)
{
  
    auto *state = static_cast<WebFetchState *>(fetch->userData);

    HttpResult result;
    result.success = (fetch->status >= 200 && fetch->status < 300);
    result.statusCode = static_cast<int>(fetch->status);
    if (fetch->numBytes > 0)
        result.body.assign(fetch->data,
                           fetch->data + fetch->numBytes);
    if (!result.success)
        result.error = "HTTP " + std::to_string(result.statusCode);

    fluxPostToUIThread(std::move(state->callback), std::move(result));

    emscripten_fetch_close(fetch);
    delete state;
}

static void onFetchError(emscripten_fetch_t *fetch)
{
    auto *state = static_cast<WebFetchState *>(fetch->userData);

    HttpResult result;
    result.success = false;
    result.statusCode = static_cast<int>(fetch->status);
    result.error = "Fetch error (status " +
                   std::to_string(fetch->status) + ")";

    fluxPostToUIThread(std::move(state->callback), std::move(result));

    emscripten_fetch_close(fetch);
    delete state;
}

// ============================================================================
// FluxHttp::globalInit / globalCleanup
// No-ops on web — no curl_global_init needed.
// ============================================================================

void FluxHttp::globalInit() {}
void FluxHttp::globalCleanup() {}

// ============================================================================
// FluxHttp::send  — the one method that actually does network I/O on web.
//
// The static FluxHttp::get / FluxHttp::post helpers declared in flux_http.hpp
// call send(), so they work unchanged.
//
// Note: emscripten_fetch attributes are stack-allocated and only need to live
// until emscripten_fetch() returns — Emscripten copies what it needs.
// ============================================================================

void FluxHttp::send(HttpRequest request,
                    HttpCallback callback,
                    bool /*postToUI*/) // always true on web
{
    auto *state = new WebFetchState;
    state->callback = std::move(callback);
    state->requestBody = request.body; // keep alive for POST/PUT/PATCH

    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);

    // ── Method ───────────────────────────────────────────────────────────────
    // emscripten_fetch_attr_t::requestMethod is char[32]
    {
        const std::string &m = request.method;
        size_t len = (std::min)(m.size(), (size_t)31);
        m.copy(attr.requestMethod, len);
        attr.requestMethod[len] = '\0';
    }

    // ── Flags ────────────────────────────────────────────────────────────────
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    // Disable IndexedDB persistence — we want a plain XHR every time.
    // (No EMSCRIPTEN_FETCH_PERSIST_FILE flag set.)

    // ── Callbacks ─────────────────────────────────────────────────────────────
    attr.onsuccess = onFetchSuccess;
    attr.onerror = onFetchError;
    attr.userData = state;

    // ── Headers ───────────────────────────────────────────────────────────────
    // Emscripten Fetch expects a null-terminated array of key/value pairs:
    //   { "Key1", "Val1", "Key2", "Val2", nullptr }
    std::vector<const char *> headerPairs;
    headerPairs.reserve(request.headers.size() * 2 + 1);
    for (auto &[k, v] : request.headers)
    {
        headerPairs.push_back(k.c_str());
        headerPairs.push_back(v.c_str());
    }
    headerPairs.push_back(nullptr);

    if (!request.headers.empty())
        attr.requestHeaders = headerPairs.data();

    // ── Body (POST / PUT / PATCH) ─────────────────────────────────────────────
    if (!state->requestBody.empty())
    {
        attr.requestData = state->requestBody.data();
        attr.requestDataSize = state->requestBody.size();
    }

    // ── Timeout ───────────────────────────────────────────────────────────────
    // Emscripten Fetch has no built-in timeout field; XHR timeout is set via
    // JS.  For now we leave it at the browser default (no timeout).
    // If you need timeouts, set them in JS via XMLHttpRequest.timeout.
    (void)request.timeoutMs;

    emscripten_fetch(&attr, request.url.c_str());
    // attr and headerPairs can go out of scope here — emscripten_fetch copies.
}

#endif // __EMSCRIPTEN__