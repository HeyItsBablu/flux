#ifdef FLUX_SSR

#include "flux/flux.hpp"
#include "flux/flux_navigator.hpp"
#include "flux/flux_hydration.hpp"
#include "flux/flux_dom_adapter.hpp"
#include "flux/flux_http.hpp"

#include <cctype>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>

// ── Socket platform shim ─────────────────────────────────────────────────
// Winsock2 needs WSAStartup/WSACleanup, a SOCKET handle type instead of
// int, closesocket() instead of close(), and setsockopt's optval as
// const char* instead of const void*. Everything else below (bind/listen/
// accept/recv/send call shapes) is identical on both platforms, so this
// shim is the only platform-conditional piece — the rest of the file is
// untouched.
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

using socket_t = SOCKET;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;

static void closeSocket(socket_t s) { closesocket(s); }
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

using socket_t = int;
constexpr socket_t kInvalidSocket = -1;

static void closeSocket(socket_t s) { close(s); }
#endif

#include <fstream>
#include <mutex>

namespace
{

    std::string readFileBinary(const std::string &path)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f)
            return {};
        return std::string((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
    }

    // The URL path fonts are served under. MUST use the exact same files
    // flux_font_ssr.cpp loads for measurement — same directory
    // (FLUX_SSR_FONT_DIR), same two filenames — just referenced by URL
    // here instead of being read+embedded per request.
    constexpr const char *kFontUrlPrefix = "/fonts/";
    constexpr const char *kRegularFontFile = "Regular.ttf";
    constexpr const char *kBoldFontFile = "Regular-Bold.ttf";

    // Built once, reused for every request's <style> block. Previously
    // this base64-encoded both TTFs and inlined them as data URIs into
    // EVERY response (~900KB of duplicate font bytes per page, with zero
    // caching benefit — the browser re-downloaded the same bytes on every
    // navigation). Now it just references the static font route below via
    // plain url(...), so the browser fetches each font file ONCE and
    // reuses it for every subsequent page on the same origin.
    const std::string &fontFaceCss()
    {
        static const std::string css = []
        {
            std::ostringstream css;
            css << "@font-face{font-family:'Inter';font-weight:400;src:url("
                << kFontUrlPrefix << kRegularFontFile << ") format('truetype');}"
                << "@font-face{font-family:'Inter';font-weight:700;src:url("
                << kFontUrlPrefix << kBoldFontFile << ") format('truetype');}";

            return css.str();
        }();
        return css;
    }

    // ── Static font file serving ──────────────────────────────────────────
    //
    // Returns true and fills outBody/outContentType if `path` matches one
    // of the two bundled font files under kFontUrlPrefix. Read from disk
    // once at startup (font files don't change while the server is
    // running) and cached in memory — same file, same bytes, every
    // request; no reason to hit the filesystem per-request.
    bool tryServeStaticFont(const std::string &path, std::string &outBody,
                            std::string &outContentType)
    {
        static const std::string dir = FLUX_SSR_FONT_DIR; // set by ssr/CMakeLists.txt
        static const std::string regularBytes = readFileBinary(dir + "/" + kRegularFontFile);
        static const std::string boldBytes = readFileBinary(dir + "/" + kBoldFontFile);

        if (path == std::string(kFontUrlPrefix) + kRegularFontFile)
        {
            if (regularBytes.empty())
            {
                std::cerr << "flux_ssr: WARNING — could not load " << kRegularFontFile
                          << " for static serving; text will render with a fallback "
                             "font whose metrics won't match layout.\n";
                return false;
            }
            outBody = regularBytes;
            outContentType = "font/ttf";
            return true;
        }
        if (path == std::string(kFontUrlPrefix) + kBoldFontFile)
        {
            if (boldBytes.empty())
            {
                std::cerr << "flux_ssr: WARNING — could not load " << kBoldFontFile
                          << " for static serving; text will render with a fallback "
                             "font whose metrics won't match layout.\n";
                return false;
            }
            outBody = boldBytes;
            outContentType = "font/ttf";
            return true;
        }
        return false;
    }
}

// Declared in the application itself (same function every platform's
// main() calls) — e.g. lib/main.cpp's createApp().
WidgetPtr createApp(FluxUI *app);

// From flux_dom_adapter_stringbuilder.cpp. Declared as IDomAdapter* here,
// not the concrete StringBuilderDomAdapter* — this TU only ever forwards
// the pointer to setActiveDomAdapter(), so it never needs the full class
// definition. StringBuilderDomAdapter itself still derives from
// IDomAdapter; only the declared signature in this TU changed.
IDomAdapter *fluxSsrCreateDomAdapter();
std::string fluxSsrSerializeDomAdapter(IDomAdapter *adapter);
void fluxSsrDestroyDomAdapter(IDomAdapter *adapter);

// From flux_painter_dom.cpp — clears the Widget*->DomNodeHandle cache.
// REQUIRED between requests: this thread reuses the same thread_local
// cache across many requests, and Widget* addresses get reused once a
// previous request's tree is destroyed — without clearing, a brand-new
// widget could collide with a freed one's stale cache entry, returning a
// DomNodeHandle that belongs to an ALREADY-DESTROYED adapter instance.
extern void fluxDomClearCacheForNewRequest();
extern void fluxDomResetNodeIdCounter();

namespace
{
    // ── JS string-literal escaping ────────────────────────────────────────
    //
    // Turns arbitrary bytes (the hydration blob may contain fetched JSON
    // text — quotes, backslashes, newlines, and possibly the two ASCII
    // control chars \x1E/\x1F flux_hydration.hpp uses as record/field
    // separators) into something safe to splice directly into
    // `Module._fluxHydrationData = "...";` inside an inline <script> tag.
    //
    // Two passes:
    //   1. Standard JS string escaping (backslash, quote, control chars).
    //   2. Escape any "</" sequence as "<\/" — the HTML PARSER (not the
    //      JS parser) scans for a literal "</script" case-insensitively
    //      regardless of what's inside a JS string; if a hydrated JSON
    //      value happened to contain the literal text "</script>" (e.g.
    //      user-generated content fetched from an API), it would
    //      prematurely close our <script> tag and corrupt the page. "\/"
    //      is a no-op escape in JS (produces a plain "/") but breaks the
    //      HTML parser's literal match.
    std::string jsStringEscape(const std::string &raw)
    {
        std::string out;
        out.reserve(raw.size() + 16);
        for (unsigned char c : raw)
        {
            switch (c)
            {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            default:
                if (c < 0x20 || c == 0x7F)
                {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\x%02X", c);
                    out += buf;
                }
                else
                {
                    out += static_cast<char>(c);
                }
            }
        }
        // Second pass: break up "</" so "</script" can never appear
        // literally in the emitted HTML, regardless of what was escaped
        // above (case above only escapes JS-meaningful characters, not
        // '<' or '/' individually).
        std::string safe;
        safe.reserve(out.size());
        for (size_t i = 0; i < out.size(); ++i)
        {
            if (out[i] == '<' && i + 1 < out.size() && out[i + 1] == '/')
            {
                safe += "<\\/";
                ++i; // consumed the '/'
            }
            else
            {
                safe += out[i];
            }
        }
        return safe;
    }

    // ── Web bundle static serving ──────────────────────────────────────────
    //
    // The SAME client WASM bundle a pure client-rendered page would load
    // (web/CMakeLists.txt's `flux_app` target: flux_app.js/.wasm/.data) —
    // Phase 5 hydration means the SSR-rendered page boots this exact
    // bundle in the background, so it must be reachable at a URL the
    // page's <script src="..."> can point to.
    //
    // FLUX_SSR_WEB_BUNDLE_DIR must be set at configure time (see
    // ssr/CMakeLists.txt) to the directory containing the built
    // flux_app.js/.wasm/.data files — typically the web build's own
    // CMAKE_CURRENT_BINARY_DIR. This couples the SSR target to a
    // successful prior (or simultaneous) web build; see the CMake diff
    // below for how that dependency is expressed.
    constexpr const char *kWebBundleUrlPrefix = "/app/";
    struct WebAssetEntry
    {
        const char *filename;
        const char *contentType;
    };
    constexpr WebAssetEntry kWebAssets[] = {
        {"flux_app.js", "application/javascript"},
        {"flux_app.wasm", "application/wasm"},
        {"flux_app.data", "application/octet-stream"},
    };

    bool tryServeWebAsset(const std::string &path, std::string &outBody,
                          std::string &outContentType)
    {
        static const std::string dir = FLUX_SSR_WEB_BUNDLE_DIR;
        for (auto &asset : kWebAssets)
        {
            if (path == std::string(kWebBundleUrlPrefix) + asset.filename)
            {
                static std::unordered_map<std::string, std::string> cache;
                static std::unordered_map<std::string, bool> warned;
                auto it = cache.find(asset.filename);
                if (it == cache.end())
                {
                    std::string bytes = readFileBinary(dir + "/" + asset.filename);
                    it = cache.emplace(asset.filename, std::move(bytes)).first;
                }
                if (it->second.empty())
                {
                    if (!warned[asset.filename])
                    {
                        warned[asset.filename] = true;
                        std::cerr << "flux_ssr: WARNING — " << asset.filename
                                  << " not found under FLUX_SSR_WEB_BUNDLE_DIR ("
                                  << dir << "); hydration will never boot on "
                                            "any page until the web bundle is built.\n";
                    }
                    return false;
                }
                outBody = it->second;
                outContentType = asset.contentType;
                return true;
            }
        }
        return false;
    }

    // Fixed viewport — no real browser window to ask. This is the same
    // known, accepted tradeoff the original roadmap flagged for
    // responsive breakpoints: one SSR render reflects one assumed
    // viewport size, not every possible client width. A later
    // enhancement (also flagged originally) would render at N
    // breakpoint widths and let CSS media queries pick the right one.
    // Tuned closer to a typical browser window's actual inner size
    // (chrome/tabs/etc already subtracted) to shrink the SSR->hydration
    // reflow — still a guess, not a fix; see comment above.
    //
    // NOTE: this is now only the LAST-RESORT fallback — see
    // resolveViewport() below, which prefers the client's REAL viewport
    // whenever it's knowable.
    constexpr int kSSRViewportWidthDefault = 1280;
    constexpr int kSSRViewportHeightDefault = 800;
    constexpr int kSSRViewportMin = 320;  // guard against bogus/hostile header values
    constexpr int kSSRViewportMax = 4096;

    std::string extractRequestPath(const std::string &requestLine)
    {
        // "GET /products/1 HTTP/1.1"
        size_t firstSpace = requestLine.find(' ');
        size_t secondSpace = requestLine.find(' ', firstSpace + 1);
        if (firstSpace == std::string::npos || secondSpace == std::string::npos)
            return "/";
        return requestLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    }

    
    // ── Request header parsing ────────────────────────────────────────────
    //
    // handleConnection() previously only looked at the request line and
    // threw the rest of the buffer away. Real headers are what let us
    // learn the client's ACTUAL viewport before rendering, instead of
    // guessing a fixed size.
    std::unordered_map<std::string, std::string> parseHeaders(std::istringstream &request)
    {
        std::unordered_map<std::string, std::string> headers;
        std::string line;
        while (std::getline(request, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                break; // blank line = end of headers
            size_t colon = line.find(':');
            if (colon == std::string::npos)
                continue;
            std::string key = line.substr(0, colon);
            std::string val = line.substr(colon + 1);
            size_t valStart = val.find_first_not_of(' ');
            val = (valStart == std::string::npos) ? "" : val.substr(valStart);
            for (auto &c : key)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            headers[key] = val;
        }
        return headers;
    }

    std::unordered_map<std::string, std::string> parseCookies(const std::string &cookieHeader)
    {
        std::unordered_map<std::string, std::string> cookies;
        size_t pos = 0;
        while (pos < cookieHeader.size())
        {
            size_t semi = cookieHeader.find(';', pos);
            std::string pair = cookieHeader.substr(
                pos, semi == std::string::npos ? std::string::npos : semi - pos);
            size_t eq = pair.find('=');
            if (eq != std::string::npos)
            {
                std::string k = pair.substr(0, eq);
                size_t kStart = k.find_first_not_of(' ');
                k = (kStart == std::string::npos) ? "" : k.substr(kStart);
                cookies[k] = pair.substr(eq + 1);
            }
            if (semi == std::string::npos)
                break;
            pos = semi + 1;
        }
        return cookies;
    }

    // ── Real viewport resolution ─────────────────────────────────────────
    //
    // Tries, in order:
    //   1. Sec-CH-Viewport-Width/Height request headers — Chromium's User-
    //      Agent Client Hints. Only present once we've asked for them via
    //      Accept-CH/Critical-CH on a PRIOR response (see handleConnection
    //      below, which forces Chromium to silently restart the very
    //      first request WITH these headers attached, before we ever
    //      render a mismatched page).
    //   2. flux_vw / flux_vh cookies — set by wrapFullPage()'s inline
    //      bootstrap script from window.innerWidth/innerHeight. Covers
    //      Firefox/Safari, which implement no viewport Client Hints at
    //      all: their first-ever visit still guesses, every visit after
    //      that is pixel-accurate.
    //   3. The hardcoded default — only reached on a true first-ever
    //      visit with neither signal available yet.
    struct ResolvedViewport
    {
        int width = kSSRViewportWidthDefault;
        int height = kSSRViewportHeightDefault;
        bool fromClientHints = false; // drives whether we (re-)send Accept-CH/Critical-CH
    };

    int clampDimension(double v)
    {
        return std::max(kSSRViewportMin,
                        std::min(kSSRViewportMax, static_cast<int>(v)));
    }

    ResolvedViewport resolveViewport(const std::unordered_map<std::string, std::string> &headers)
    {
        ResolvedViewport out;

        auto chW = headers.find("sec-ch-viewport-width");
        auto chH = headers.find("sec-ch-viewport-height");
        if (chW != headers.end() && chH != headers.end())
        {
            try
            {
                out.width = clampDimension(std::stod(chW->second));
                out.height = clampDimension(std::stod(chH->second));
                out.fromClientHints = true;
                return out;
            }
            catch (...)
            {
                // Malformed header — fall through to cookie/default.
            }
        }

        auto cookieHeader = headers.find("cookie");
        if (cookieHeader != headers.end())
        {
            auto cookies = parseCookies(cookieHeader->second);
            auto vw = cookies.find("flux_vw");
            auto vh = cookies.find("flux_vh");
            if (vw != cookies.end() && vh != cookies.end())
            {
                try
                {
                    out.width = clampDimension(std::stod(vw->second));
                    out.height = clampDimension(std::stod(vh->second));
                }
                catch (...)
                {
                    // Malformed cookie — fall back to default.
                }
            }
        }

        return out;
    }

    // hydrationBlob: base64-encoded output of fluxHydrationSerializeBlob(),
    // already collected by the time this is called (see renderRequest below).
    std::string wrapFullPage(const std::string &bodyHtml, const std::string &hydrationBlob)
    {
        std::ostringstream html;
        html << "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
             << "<style>*{margin:0;padding:0;box-sizing:border-box;}"
             << "html,body{width:100%;height:100%;font-family:'Inter',sans-serif;}"
             << fontFaceCss()
             << "</style></head><body>"
             // id="flux-dom-root" — REQUIRED. flux_dom_adapter_live.cpp's
             // setRoot()/Module._fluxDomAdopt() both look up this exact id
             // (matching shell.html.in's #flux-dom-root, see that file) to
             // find the server-rendered content to mount into / adopt from.
             << "<div id=\"flux-dom-root\" style=\"position:relative;width:100%;height:100%;\">"
             << bodyHtml
         // #flux-input-capture — REQUIRED for the page to be interactive
         // at all post-hydration. flux_window_dom.cpp registers every
         // mouse/touch/keyboard listener on this exact element selector
         // (see emscripten_set_mousedown_callback("#flux-input-capture",
         // ...) etc). Without it, hydration completes and the page LOOKS
         // interactive but every click silently does nothing — no error,
         // no console warning, just a dead page. Matches the markup
         // web/CMakeLists.txt's FLUX_CANVAS_RENDERER_MARKUP generates for
         // FLUX_WEB_RENDERER=dom builds.
         << "<div id=\"flux-input-capture\" style=\"position:absolute;top:0;left:0;"
            "width:100%;height:100%;touch-action:none;background:transparent;z-index:1;\"></div>"
         // #flux-gl — present on both renderers; CanvasWidget/video/camera
         // widgets use it regardless of which renderer draws everything
         // else. Empty/inert until such a widget attaches to it.
         << "<canvas id=\"flux-gl\" style=\"position:absolute;top:0;left:0;"
            "width:100%;height:100%;pointer-events:none;\"></canvas>"
         // ── Minimal Module bootstrap ──────────────────────────────────
         // Deliberately NOT the full shell.html.in bootstrap (no loading
         // spinner needed — the whole point of SSR is that real content
         // is already visible; no error overlay wiring either, kept out
         // for now as a known simplification). Just enough for
         // web/main.cpp's main() to boot correctly against THIS
         // document: canvas/DPR globals it reads at startup, and the
         // hydration blob it reads before build().
         << "<script>"
            "var Module = {};"
            "Module.canvas = document.getElementById('flux-gl');"
            "Module._fluxDPR = window.devicePixelRatio || 1;"
            // Physical (device) pixels — canvas backing-store size only.
            // NOT what the DOM renderer should lay out at: applyRect() in
            // flux_painter_dom.cpp writes raw px values straight into CSS,
            // which the browser always interprets as CSS/logical pixels.
            // Booting the DOM renderer's FluxUI window from these on any
            // DPR>1 display (basically all HiDPI screens) lays the whole
            // tree out at ~DPR-times too large — which is exactly what
            // hydration's "page jumps to a bigger size" symptom is.
            "Module._fluxPhysicalWidth = Math.floor(window.innerWidth * Module._fluxDPR);"
            "Module._fluxPhysicalHeight = Math.floor(window.innerHeight * Module._fluxDPR);"
            // Logical (CSS) pixels — what the DOM renderer must boot at,
            // to match the SSR pass, which resolved its own viewport in
            // logical px the whole way through (Sec-CH-Viewport-*, the
            // flux_vw/vh cookies below, and kSSRViewportWidthDefault are
            // all logical-px values, never DPR-multiplied).
            "Module._fluxLogicalWidth = window.innerWidth;"
            "Module._fluxLogicalHeight = window.innerHeight;"
            // Fallback path for browsers with no viewport Client Hints
            // (Firefox, Safari): remember the REAL viewport for next
            // time, so resolveViewport() can use it instead of guessing.
            // Chromium doesn't need this — it already told us via
            // Sec-CH-Viewport-* headers before this response was even
            // generated (see the Critical-CH handling in
            // handleConnection() below).
            "document.cookie = 'flux_vw=' + window.innerWidth + ';path=/;max-age=86400;SameSite=Lax';"
            "document.cookie = 'flux_vh=' + window.innerHeight + ';path=/;max-age=86400;SameSite=Lax';"
            "Module.locateFile = function(path, prefix) { return '" << kWebBundleUrlPrefix << "' + path; };"
            // The hydration payload itself — see jsStringEscape()'s
            // header comment for why this is safe to splice in raw
            // rather than needing a separate encoding step.
            "Module._fluxHydrationData = \"" << jsStringEscape(hydrationBlob) << "\";"
         << "</script>"
         << "<script src=\"" << kWebBundleUrlPrefix << "flux_app.js\"></script>"
             << "</body></html>";
        return html.str();
    }

    // One request, start to finish. Returns the full HTML response body.
    // viewportWidth/viewportHeight come from resolveViewport() in
    // handleConnection() — the ACTUAL client viewport whenever it's
    // knowable, not a fixed guess.
    std::string renderRequest(const std::string &requestedPath,
                              int viewportWidth, int viewportHeight)
    {
        // ── Reset ALL per-request thread_local state ─────────────────────
        fluxSetSSRSyncMode(true);
        fluxHydrationClear();
        fluxHydrationResetIdCounter();
        fluxDomResetNodeIdCounter();
        fluxDomClearCacheForNewRequest();
        Navigator::setSSRRequestPath(requestedPath);

        IDomAdapter *adapter = fluxSsrCreateDomAdapter();
        setActiveDomAdapter(adapter);

        std::string html;
        {
            // Scoped so FluxUI (and, transitively, FluxAppWidget/the
            // whole widget tree) are FULLY destroyed before this
            // function returns — required for the thread_local
            // FluxAppWidget::instance_/FluxUI::currentInstance
            // "one alive instance per thread" invariants (Phase 0) to
            // hold across the NEXT request on this same thread.
            FluxUI fluxUI(nullptr);
            fluxUI.createWindow("", viewportWidth, viewportHeight);
            fluxUI.build([&]()
                         { return createApp(&fluxUI); });

            // One manual render pass — no tick()/main-loop exists on this
            // platform (see flux_window_headless.cpp). This directly
            // invokes the exact same onPaint callback flux_core.cpp's
            // wireCallbacks() already wired up for every other platform.
            GraphicsContext ctx(viewportWidth, viewportHeight);
            if (fluxUI.getPlatformWindow().callbacks.onPaint)
                fluxUI.getPlatformWindow().callbacks.onPaint(
                    ctx, viewportWidth, viewportHeight);

            html = fluxSsrSerializeDomAdapter(adapter);
        }

        setActiveDomAdapter(nullptr);
        fluxSsrDestroyDomAdapter(adapter);
        fluxSetSSRSyncMode(false);

        
        return wrapFullPage(html, fluxHydrationSerializeBlob());
    }

    void handleConnection(socket_t clientFd)
    {
        char buf[8192];
        int n = recv(clientFd, buf, sizeof(buf) - 1, 0);
        if (n <= 0)
        {
            closeSocket(clientFd);
            return;
        }
        buf[n] = '\0';

        std::istringstream request(buf);
        std::string requestLine;
        std::getline(request, requestLine);
        std::string path = extractRequestPath(requestLine);
        std::unordered_map<std::string, std::string> headers = parseHeaders(request);

        std::string body;
        int statusCode = 200;
        std::string contentType = "text/html; charset=utf-8";
        bool cacheable = false;
        bool isRenderedPage = false;
        bool viewportFromClientHints = false;
        try
        {
            if (tryServeStaticFont(path, body, contentType))
            {
                cacheable = true;
            }
            else if (tryServeWebAsset(path, body, contentType))
            {
                // NOT marked cacheable=true (no long-lived Cache-Control)
                // unlike the fonts. Rebuilding the app produces NEW bytes
                // under the SAME filename (flux_app.js/.wasm/.data have
                // no content hash in their name, unlike a typical
                // webpack/vite build) — an aggressive immutable cache
                // header here would strand returning visitors on a stale
                // bundle after every deploy. Left uncached for now;
                // revisit with hashed filenames if this needs to scale.
            }
            else
            {
                isRenderedPage = true;
                ResolvedViewport vp = resolveViewport(headers);
                viewportFromClientHints = vp.fromClientHints;
                body = renderRequest(path, vp.width, vp.height);
            }
        }
        catch (const std::exception &e)
        {
            statusCode = 500;
            body = std::string("Internal Server Error: ") + e.what();
            contentType = "text/html; charset=utf-8";
            std::cerr << "flux_ssr: render failed for '" << path << "': " << e.what() << "\n";
        }
        catch (...)
        {
            statusCode = 500;
            body = "Internal Server Error";
            contentType = "text/html; charset=utf-8";
            std::cerr << "flux_ssr: render failed for '" << path << "' (unknown exception)\n";
        }

        std::ostringstream response;
        response << "HTTP/1.1 " << statusCode << (statusCode == 200 ? " OK" : " Internal Server Error") << "\r\n"
                 << "Content-Type: " << contentType << "\r\n"
                 << "Content-Length: " << body.size() << "\r\n";
        if (cacheable)
        {
            // Font bytes never change while the server is running (read
            // once at startup, see tryServeStaticFont) — safe to let the
            // browser cache them for a long time instead of refetching on
            // every navigation. If the bundled font files are ever
            // swapped on disk, restart the server (or add a versioned
            // filename/query string later if hot-swapping is needed).
            response << "Cache-Control: public, max-age=31536000, immutable\r\n";
        }
        if (isRenderedPage && !viewportFromClientHints)
        {
            // Ask Chromium for the client's real viewport before we ever
            // render again. Accept-CH advertises which hints we want;
            // Critical-CH additionally forces THIS request to silently
            // restart (before the browser even shows this body) once
            // those hints are available — so even a first-ever Chromium
            // visit ends up rendering against the real viewport instead
            // of the fallback default. No effect on Firefox/Safari (they
            // ignore both headers) — those rely on the flux_vw/vh cookie
            // fallback instead (see resolveViewport() /
            // wrapFullPage()'s bootstrap script).
            response << "Accept-CH: Sec-CH-Viewport-Width, Sec-CH-Viewport-Height\r\n"
                     << "Critical-CH: Sec-CH-Viewport-Width, Sec-CH-Viewport-Height\r\n"
                     << "Vary: Sec-CH-Viewport-Width, Sec-CH-Viewport-Height, Cookie\r\n";
        }
        response << "Connection: close\r\n\r\n"
                 << body;

        std::string out = response.str();
        send(clientFd, out.c_str(), static_cast<int>(out.size()), 0);
        closeSocket(clientFd);
    }
}

int main(int argc, char **argv)
{
    int port = (argc > 1) ? std::atoi(argv[1]) : 8080;

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "flux_ssr: WSAStartup() failed\n";
        return 1;
    }
#endif

    FluxHttp::globalInit();

    socket_t listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd == kInvalidSocket)
    {
        std::cerr << "flux_ssr: socket() failed\n";
        return 1;
    }

    int opt = 1;
    // const char* cast keeps this one line valid on both platforms —
    // POSIX setsockopt() takes const void* (accepts const char* fine);
    // Winsock's setsockopt() requires const char* specifically.
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char *>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<unsigned short>(port));

    if (bind(listenFd, (sockaddr *)&addr, sizeof(addr)) < 0)
    {
        std::cerr << "flux_ssr: bind() failed on port " << port << "\n";
        return 1;
    }
    listen(listenFd, 16);

    std::cout << "flux_ssr listening on http://0.0.0.0:" << port
              << " (single-threaded — see Phase 6 for concurrency)\n";

    // Sequential accept loop — deliberately simple. Every request is
    // fully handled, start to finish, on this one thread before the next
    // accept() runs, which is exactly what makes the thread_local reset
    // discipline above sufficient for correctness right now.
    for (;;)
    {
        socket_t clientFd = accept(listenFd, nullptr, nullptr);
        if (clientFd == kInvalidSocket)
            continue;
        handleConnection(clientFd);
    }

    FluxHttp::globalCleanup();
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}

#endif // FLUX_SSR