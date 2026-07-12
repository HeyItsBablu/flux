#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifndef __EMSCRIPTEN__
#include <curl/curl.h>
#endif

// ============================================================================
// HTTP REQUEST / RESPONSE
// ============================================================================

struct HttpRequest
{
    std::string url;
    std::string method = "GET"; // GET POST PUT PATCH DELETE
    std::string body;
    std::map<std::string,
             std::string>
        headers;
    int timeoutMs = 10000;
    bool verifySsl = true;
};

struct HttpResult
{
    bool success = false;
    int statusCode = 0;
    std::string body;
    std::string error;
};

// ============================================================================
// PLATFORM THREAD-MARSHAL CALLBACK
// Each platform posts the result back to the UI thread differently.
// Provide one implementation per platform.
// ============================================================================

using HttpCallback = std::function<void(HttpResult)>;

// Forward declaration — implemented per-platform in flux_http_platform.hpp
void fluxPostToUIThread(HttpCallback callback, HttpResult result);

// SSR SYNCHRONOUS MODE
//
// Named generically (not "Http-specific") because the same problem — no
// event loop to deliver an async result to during a one-shot SSR render —
// applies to any backgrounded work a widget kicks off, not just HTTP
// requests. ImageWidget's local-file loading (flux_image.hpp) uses this
// same flag for exactly that reason: it never touches FluxHttp at all,
// but needs the identical "run inline, right now" behavior during SSR.
//
// A one-shot, single-pass SSR render has no running event loop / message
// pump to later deliver an async HTTP result to — fluxPostToUIThread's
// per-platform implementations (PeekMessage, SDL_PushEvent, ALooper pipe,
// GCD main queue) all assume something is actively draining them. On the
// SSR host, nothing is.
//
// When this flag is set, FluxHttp::send() (native implementation only —
// see below) skips the background thread entirely and calls perform()
// inline, blocking the calling thread until the request completes, then
// invokes the callback directly. This is safe specifically because the
// SSR render model is "one thread owns one request's full render, start
// to finish" — see the thread_local fixes elsewhere in Phase 0 for the
// same underlying assumption.
//
// thread_local so the SSR host can turn this on only for the duration of
// rendering one request, on the thread handling that request, without
// affecting any other concurrently-rendering request/thread.
// ============================================================================

inline thread_local bool g_fluxSSRSyncMode = false;

inline void fluxSetSSRSyncMode(bool enabled) { g_fluxSSRSyncMode = enabled; }
inline bool fluxSSRSyncModeEnabled() { return g_fluxSSRSyncMode; }

// ============================================================================
// SSR PARALLEL-FETCH CACHE (GET only, request-scoped)
//
// Wave-based parallel fetching for SSR: ssr/main.cpp's renderRequest()
// rebuilds the ENTIRE widget tree from scratch (calls createApp() again)
// in a loop, "waves", until nothing new is pending. This cache is what
// makes that safe rather than wasteful or order-breaking:
//
//   - On each fresh rebuild, a GET to a URL already resolved by an
//     earlier wave's parallel drain hits the cache and resolves
//     SYNCHRONOUSLY, INLINE, in the same call stack — exactly like the
//     original always-blocking design. This is what keeps
//     FutureBuilderWidget hydration-ID construction order matching the
//     client's single-pass hydration walk (see ssr/main.cpp's
//     renderRequest() header comment for the full ID-ordering proof —
//     a naive "re-layout the persistent tree" approach gets this wrong).
//   - A GET not yet resolved is recorded as pending and returns WITHOUT
//     invoking its callback this wave — the requesting widget simply
//     stays in its "still loading" state for this (throwaway) tree.
//   - Only GET is cached/deferred. Every other method keeps the
//     original always-blocks-immediately behavior, so nothing that
//     might have side effects (POST/PUT/PATCH/DELETE) is ever silently
//     deduplicated or deferred across waves.
//   - Scoped to ONE request's lifetime — ssrClearUrlCache() must be
//     called once before a request's first wave, and never again mid-
//     request. Repeated GETs to the same URL within one SSR render are
//     therefore deduplicated; if a page genuinely needs a fresh value
//     from the same URL more than once in a single render, that's a
//     cache-busting-query-param problem, not something this layer
//     tries to solve.
// ============================================================================

inline thread_local std::unordered_map<std::string, HttpResult> g_ssrUrlCache;
inline thread_local std::unordered_map<std::string, HttpRequest> g_ssrPendingByUrl;

// ============================================================================
// FLUX HTTP
// ============================================================================

class FluxHttp
{
public:
    // ── One-shot convenience calls ──────────────────────────────────────────
    static void get(const std::string &url,
                    HttpCallback callback,
                    bool postToUI = true)
    {
        HttpRequest req;
        req.url = url;
        req.method = "GET";
        send(req, std::move(callback), postToUI);
    }

    static void post(const std::string &url,
                     const std::string &body,
                     HttpCallback callback,
                     bool postToUI = true)
    {
        HttpRequest req;
        req.url = url;
        req.method = "POST";
        req.body = body;
        req.headers["Content-Type"] = "application/json";
        send(req, std::move(callback), postToUI);
    }

    // ── Full request ─────────────────────────────────────────────────────────
    // On web: implemented in flux_http_web.cpp (Emscripten Fetch API).
    // On native: implemented below using libcurl on a detached thread.
    static void send(HttpRequest request,
                     HttpCallback callback,
                     bool postToUI = true);
    // ── SSR parallel-fetch drain (native only; see cache comment above) ────
    //
    // Fetches every URL recorded as pending since the last drain, ALL AT
    // ONCE on separate threads, joins them, then folds every result into
    // the resolved-URL cache. Callbacks are NEVER invoked from the worker
    // threads spawned here — only perform() (pure, stateless, touches no
    // widget/DOM/hydration state) runs off-thread. Every actual callback
    // invocation happens later, back on the caller's own thread, the next
    // time send() sees a cache hit during the next rebuild wave — keeping
    // the "only the request's own thread ever touches its widgets" rule
    // from Phase 0 intact.
    //
    // Returns the number of URLs fetched this call — ssr/main.cpp's wave
    // loop keeps rebuilding while this is nonzero, and stops (renders)
    // once a rebuild produces zero new pending fetches.
    static int ssrDrainPendingFetches();

    // Clears both the resolved cache and the pending set. Call ONCE per
    // SSR request, before its first wave — never mid-request, or a later
    // wave would lose visibility into an earlier wave's resolved results.
    static void ssrClearUrlCache()
    {
        g_ssrUrlCache.clear();
        g_ssrPendingByUrl.clear();
    }

    // ── Global init / cleanup (call once at app start/end) ───────────────────
    // No-ops on web; curl_global_init / curl_global_cleanup on native.
    static void globalInit();
    static void globalCleanup();

    static void setCABundle(const std::string &path) { s_caBundle_ = path; }
    static const std::string &getCABundle() { return s_caBundle_; }

private:
    static inline std::string s_caBundle_;

#ifndef __EMSCRIPTEN__
    // ── libcurl write callback ────────────────────────────────────────────────
    static size_t writeCallback(char *ptr, size_t size,
                                size_t nmemb, void *userdata)
    {
        auto *buf = static_cast<std::string *>(userdata);
        buf->append(ptr, size * nmemb);
        return size * nmemb;
    }

    // ── Synchronous perform — runs on background thread ───────────────────────
    static HttpResult perform(const HttpRequest &req)
    {
        HttpResult res;

        CURL *curl = curl_easy_init();
        if (!curl)
        {
            res.error = "curl_easy_init failed";
            return res;
        }

        // ── Response body buffer ─────────────────────────────────────────────
        std::string responseBody;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);

        // ── SSL ───────────────────────────────────────────────────────────────
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, req.verifySsl ? 1L : 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, req.verifySsl ? 2L : 0L);
        if (!s_caBundle_.empty())
            curl_easy_setopt(curl, CURLOPT_CAINFO, s_caBundle_.c_str());

        // ── URL + method ─────────────────────────────────────────────────────
        curl_easy_setopt(curl, CURLOPT_URL, req.url.c_str());

        if (req.method == "POST")
        {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)req.body.size());
        }
        else if (req.method == "PUT")
        {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)req.body.size());
        }
        else if (req.method == "PATCH")
        {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)req.body.size());
        }
        else if (req.method == "DELETE")
        {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        }
        // GET is the default — no extra opt needed

        // ── Headers ──────────────────────────────────────────────────────────
        curl_slist *headerList = nullptr;
        for (auto &[k, v] : req.headers)
        {
            std::string h = k + ": " + v;
            headerList = curl_slist_append(headerList, h.c_str());
        }
        if (headerList)
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);

        // ── Timeout ───────────────────────────────────────────────────────────
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)req.timeoutMs);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, (long)(req.timeoutMs / 2));

        // ── Follow redirects ──────────────────────────────────────────────────
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);

        // ── Perform ──────────────────────────────────────────────────────────
        CURLcode cc = curl_easy_perform(curl);

        if (cc != CURLE_OK)
        {
            res.error = curl_easy_strerror(cc);
        }
        else
        {
            long status = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
            res.statusCode = (int)status;
            res.body = std::move(responseBody);
            res.success = (res.statusCode >= 200 && res.statusCode < 300);
            if (!res.success && res.error.empty())
                res.error = "HTTP " + std::to_string(res.statusCode);
        }

        if (headerList)
            curl_slist_free_all(headerList);
        curl_easy_cleanup(curl);
        return res;
    }
#endif // !__EMSCRIPTEN__
};

// ============================================================================
// NATIVE send / globalInit / globalCleanup
// On web these three are defined in flux_http_web.cpp instead.
// ============================================================================

#ifndef __EMSCRIPTEN__

inline void FluxHttp::globalInit() { curl_global_init(CURL_GLOBAL_DEFAULT); }
inline void FluxHttp::globalCleanup() { curl_global_cleanup(); }

inline int FluxHttp::ssrDrainPendingFetches()
{
    auto pending = std::move(g_ssrPendingByUrl);
    g_ssrPendingByUrl.clear();
    if (pending.empty())
        return 0;

    std::vector<std::string> urls;
    std::vector<HttpRequest> reqs;
    urls.reserve(pending.size());
    reqs.reserve(pending.size());
    for (auto &[url, req] : pending)
    {
        urls.push_back(url);
        reqs.push_back(std::move(req));
    }

    // One thread per pending URL. Deliberately unbounded for this
    // iteration — matches the roadmap's own Phase 6 note that a bounded
    // pool is a documented follow-up, not solved here. A page with an
    // unusually large fan-out of independent fetches in one wave will
    // spawn that many threads at once; fine for realistic page sizes,
    // worth revisiting before this sees pages with dozens+ of parallel
    // data sources.
    std::vector<HttpResult> results(reqs.size());
    std::vector<std::thread> workers;
    workers.reserve(reqs.size());
    for (size_t i = 0; i < reqs.size(); ++i)
        workers.emplace_back([&reqs, &results, i]
                             { results[i] = perform(reqs[i]); });
    for (auto &t : workers)
        t.join();

    for (size_t i = 0; i < urls.size(); ++i)
        g_ssrUrlCache[urls[i]] = std::move(results[i]);

    return (int)urls.size();
}

inline void FluxHttp::send(HttpRequest request,
                           HttpCallback callback,
                           bool postToUI)
{

    if (fluxSSRSyncModeEnabled())
    {
        if (request.method == "GET")
        {
            auto it = g_ssrUrlCache.find(request.url);
            if (it != g_ssrUrlCache.end())
            {
                // Resolved by an earlier wave's drain — resolve inline,
                // synchronously, right now. This is the case that keeps
                // hydration-ID construction order correct across waves;
                // see the cache's header comment above.
                if (callback)
                    callback(it->second);
                return;
            }
            // Not yet resolved — defer to the next drain. Callback is
            // deliberately NOT invoked this pass; the widget requesting
            // it stays in its "still loading" state for this wave's
            // (throwaway) tree.
            g_ssrPendingByUrl.emplace(request.url, std::move(request));
            return;
        }
        // Non-GET: unchanged original behavior — always blocks inline,
        // never cached or deferred, so nothing with side effects is
        // ever silently deduplicated across rebuild waves.
        HttpResult result = perform(request);
        if (callback)
            callback(std::move(result));
        return;
    }

    std::thread([request = std::move(request),
                 callback = std::move(callback),
                 postToUI]() mutable
                {
        HttpResult result = perform(request);

        if (postToUI)
            fluxPostToUIThread(std::move(callback), std::move(result));
        else if (callback)
            callback(result); })
        .detach();
}

#endif // !__EMSCRIPTEN__