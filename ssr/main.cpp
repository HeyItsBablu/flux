#ifdef FLUX_SSR

#include "flux/flux.hpp"
#include "flux/flux_navigator.hpp"
#include "flux/flux_hydration.hpp"
#include "flux/flux_dom_adapter.hpp"
#include "flux/flux_http.hpp"

#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

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
        if (!f) return {};
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

namespace
{
    // Fixed viewport — no real browser window to ask. This is the same
    // known, accepted tradeoff the original roadmap flagged for
    // responsive breakpoints: one SSR render reflects one assumed
    // viewport size, not every possible client width. A later
    // enhancement (also flagged originally) would render at N
    // breakpoint widths and let CSS media queries pick the right one.
    constexpr int kSSRViewportWidth = 1280;
    constexpr int kSSRViewportHeight = 800;

    std::string extractRequestPath(const std::string &requestLine)
    {
        // "GET /products/1 HTTP/1.1"
        size_t firstSpace = requestLine.find(' ');
        size_t secondSpace = requestLine.find(' ', firstSpace + 1);
        if (firstSpace == std::string::npos || secondSpace == std::string::npos)
            return "/";
        return requestLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    }

std::string wrapFullPage(const std::string &bodyHtml)
{
    std::ostringstream html;
    html << "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">"
         << "<style>*{margin:0;padding:0;box-sizing:border-box;}"
         << "html,body{width:100%;height:100%;font-family:'Inter',sans-serif;}"
         << fontFaceCss()
         << "</style></head><body>"
         << "<div style=\"position:relative;width:100%;height:100%;\">"
         << bodyHtml
         << "</div></body></html>";
    return html.str();
}

    // One request, start to finish. Returns the full HTML response body.
    std::string renderRequest(const std::string &requestedPath)
    {
        // ── Reset ALL per-request thread_local state ─────────────────────
        fluxSetSSRSyncMode(true);
        fluxHydrationClear();
        fluxHydrationResetIdCounter();
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
            fluxUI.createWindow("", kSSRViewportWidth, kSSRViewportHeight);
            fluxUI.build([&]()
                         { return createApp(&fluxUI); });

            // One manual render pass — no tick()/main-loop exists on this
            // platform (see flux_window_headless.cpp). This directly
            // invokes the exact same onPaint callback flux_core.cpp's
            // wireCallbacks() already wired up for every other platform.
            GraphicsContext ctx(kSSRViewportWidth, kSSRViewportHeight);
            if (fluxUI.getPlatformWindow().callbacks.onPaint)
                fluxUI.getPlatformWindow().callbacks.onPaint(
                    ctx, kSSRViewportWidth, kSSRViewportHeight);

            html = fluxSsrSerializeDomAdapter(adapter);
        }

        setActiveDomAdapter(nullptr);
        fluxSsrDestroyDomAdapter(adapter);
        fluxSetSSRSyncMode(false);

        return wrapFullPage(html);
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

        std::string body;
        int statusCode = 200;
        std::string contentType = "text/html; charset=utf-8";
        bool cacheable = false;
        try
        {
            if (tryServeStaticFont(path, body, contentType))
            {
                cacheable = true;
            }
            else
            {
                body = renderRequest(path);
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