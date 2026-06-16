#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>

#ifndef __EMSCRIPTEN__
#  include <curl/curl.h>
#endif

// ============================================================================
// HTTP REQUEST / RESPONSE
// ============================================================================

struct HttpRequest {
    std::string              url;
    std::string              method   = "GET";   // GET POST PUT PATCH DELETE
    std::string              body;
    std::map<std::string,
             std::string>    headers;
    int                      timeoutMs = 10000;
    bool                     verifySsl = true;
};

struct HttpResult {
    bool        success    = false;
    int         statusCode = 0;
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

// ============================================================================
// FLUX HTTP
// ============================================================================

class FluxHttp {
public:
    // ── One-shot convenience calls ──────────────────────────────────────────
    static void get(const std::string& url,
                    HttpCallback       callback,
                    bool               postToUI = true)
    {
        HttpRequest req;
        req.url    = url;
        req.method = "GET";
        send(req, std::move(callback), postToUI);
    }

    static void post(const std::string& url,
                     const std::string& body,
                     HttpCallback       callback,
                     bool               postToUI = true)
    {
        HttpRequest req;
        req.url    = url;
        req.method = "POST";
        req.body   = body;
        req.headers["Content-Type"] = "application/json";
        send(req, std::move(callback), postToUI);
    }

    // ── Full request ─────────────────────────────────────────────────────────
    // On web: implemented in flux_http_web.cpp (Emscripten Fetch API).
    // On native: implemented below using libcurl on a detached thread.
    static void send(HttpRequest  request,
                     HttpCallback callback,
                     bool         postToUI = true);

    // ── Global init / cleanup (call once at app start/end) ───────────────────
    // No-ops on web; curl_global_init / curl_global_cleanup on native.
    static void globalInit();
    static void globalCleanup();

    static void setCABundle(const std::string& path) { s_caBundle_ = path; }
    static const std::string& getCABundle()          { return s_caBundle_; }

private:
    static inline std::string s_caBundle_;

#ifndef __EMSCRIPTEN__
    // ── libcurl write callback ────────────────────────────────────────────────
    static size_t writeCallback(char* ptr, size_t size,
                                size_t nmemb, void* userdata)
    {
        auto* buf = static_cast<std::string*>(userdata);
        buf->append(ptr, size * nmemb);
        return size * nmemb;
    }

    // ── Synchronous perform — runs on background thread ───────────────────────
    static HttpResult perform(const HttpRequest& req)
    {
        HttpResult res;

        CURL* curl = curl_easy_init();
        if (!curl) {
            res.error = "curl_easy_init failed";
            return res;
        }

        // ── Response body buffer ─────────────────────────────────────────────
        std::string responseBody;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &responseBody);

        // ── SSL ───────────────────────────────────────────────────────────────
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, req.verifySsl ? 1L : 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, req.verifySsl ? 2L : 0L);
        if (!s_caBundle_.empty())
            curl_easy_setopt(curl, CURLOPT_CAINFO, s_caBundle_.c_str());

        // ── URL + method ─────────────────────────────────────────────────────
        curl_easy_setopt(curl, CURLOPT_URL, req.url.c_str());

        if (req.method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS,    req.body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)req.body.size());
        } else if (req.method == "PUT") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS,    req.body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)req.body.size());
        } else if (req.method == "PATCH") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS,    req.body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)req.body.size());
        } else if (req.method == "DELETE") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        }
        // GET is the default — no extra opt needed

        // ── Headers ──────────────────────────────────────────────────────────
        curl_slist* headerList = nullptr;
        for (auto& [k, v] : req.headers) {
            std::string h = k + ": " + v;
            headerList = curl_slist_append(headerList, h.c_str());
        }
        if (headerList)
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);

        // ── Timeout ───────────────────────────────────────────────────────────
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS,        (long)req.timeoutMs);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, (long)(req.timeoutMs / 2));

        // ── Follow redirects ──────────────────────────────────────────────────
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS,      5L);

        // ── Perform ──────────────────────────────────────────────────────────
        CURLcode cc = curl_easy_perform(curl);

        if (cc != CURLE_OK) {
            res.error = curl_easy_strerror(cc);
        } else {
            long status = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
            res.statusCode = (int)status;
            res.body       = std::move(responseBody);
            res.success    = (res.statusCode >= 200 && res.statusCode < 300);
            if (!res.success && res.error.empty())
                res.error = "HTTP " + std::to_string(res.statusCode);
        }

        if (headerList) curl_slist_free_all(headerList);
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

inline void FluxHttp::globalInit()    { curl_global_init(CURL_GLOBAL_DEFAULT); }
inline void FluxHttp::globalCleanup() { curl_global_cleanup(); }

inline void FluxHttp::send(HttpRequest  request,
                            HttpCallback callback,
                            bool         postToUI)
{
    std::thread([request  = std::move(request),
                 callback = std::move(callback),
                 postToUI]() mutable
    {
        HttpResult result = perform(request);

        if (postToUI)
            fluxPostToUIThread(std::move(callback), std::move(result));
        else if (callback)
            callback(result);
    }).detach();
}

#endif // !__EMSCRIPTEN__