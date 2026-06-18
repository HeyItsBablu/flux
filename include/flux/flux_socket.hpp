#pragma once

#include "flux_http_platform.hpp"
#include "flux_http.hpp"

#include <atomic>
#include <functional>
#include <string>
#include <thread>

class FluxSocket
{
public:
    std::function<void()> onOpen;
    std::function<void(std::string)> onMessage;
    std::function<void(std::string)> onError;
    std::function<void(int)> onClose;

    FluxSocket() = default;
    ~FluxSocket() { close(); }

    FluxSocket(const FluxSocket &) = delete;
    FluxSocket &operator=(const FluxSocket &) = delete;

    void connect(const std::string &url);
    void send(const std::string &message);
    void close();
    bool isConnected() const { return connected_; }

private:
    std::atomic<bool> connected_{false};

#ifndef __EMSCRIPTEN__
    // ── libcurl members (native only) ─────────────────────────────────────
    CURL *curl_ = nullptr;
    std::thread thread_;
    std::atomic<bool> running_{false};

    void receiveLoop(const std::string &url);
    void wsSend(const std::string &msg);
    void cleanupCurl();

    void postOpen();
    void postMessage(std::string msg);
    void postError(std::string err);
    void postClose(int code);
#else
    // ── Emscripten members (web only) ─────────────────────────────────────
    int wsHandle_ = -1; // index into the JS-side socket table
#endif
};

// ============================================================================
// NATIVE IMPLEMENTATION  (libcurl)
// ============================================================================

#ifndef __EMSCRIPTEN__

inline void FluxSocket::connect(const std::string &url)
{
    close();
    running_ = true;
    connected_ = false;
    thread_ = std::thread([this, url]()
                          { receiveLoop(url); });
}

inline void FluxSocket::send(const std::string &message)
{
    if (!connected_)
        return;
    wsSend(message);
}

inline void FluxSocket::close()
{
    running_ = false;
    if (curl_)
        curl_ws_send(curl_, nullptr, 0, nullptr, 0, CURLWS_CLOSE);
    if (thread_.joinable())
        thread_.join();
    cleanupCurl();
    connected_ = false;
}

inline void FluxSocket::receiveLoop(const std::string &url)
{
    curl_ = curl_easy_init();
    if (!curl_)
    {
        postError("curl_easy_init failed");
        return;
    }

    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_CONNECT_ONLY, 2L);
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);

    const std::string &caBundle = FluxHttp::getCABundle();
    if (!caBundle.empty())
        curl_easy_setopt(curl_, CURLOPT_CAINFO, caBundle.c_str());

    CURLcode cc = curl_easy_perform(curl_);
    if (cc != CURLE_OK)
    {
        postError(curl_easy_strerror(cc));
        cleanupCurl();
        return;
    }

    connected_ = true;
    postOpen();

    std::string fragmentBuf;
    while (running_)
    {
        char buf[4096];
        size_t bytesRecv = 0;
        const curl_ws_frame *meta = nullptr;

        cc = curl_ws_recv(curl_, buf, sizeof(buf), &bytesRecv, &meta);

        if (cc == CURLE_AGAIN)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if (cc != CURLE_OK)
        {
            if (running_)
                postError(curl_easy_strerror(cc));
            break;
        }
        if (!meta)
            continue;

        if (meta->flags & CURLWS_CLOSE)
        {
            int code = 1000;
            if (bytesRecv >= 2)
                code = ((unsigned char)buf[0] << 8) | (unsigned char)buf[1];
            postClose(code);
            break;
        }

        if (meta->flags & CURLWS_TEXT)
        {
            fragmentBuf.append(buf, bytesRecv);
            if (meta->bytesleft == 0)
            {
                postMessage(std::move(fragmentBuf));
                fragmentBuf.clear();
            }
        }
    }
    cleanupCurl();
}

inline void FluxSocket::wsSend(const std::string &msg)
{
    if (!curl_)
        return;
    size_t sent = 0;
    curl_ws_send(curl_, msg.data(), msg.size(), &sent, 0, CURLWS_TEXT);
}

inline void FluxSocket::cleanupCurl()
{
    if (curl_)
    {
        curl_easy_cleanup(curl_);
        curl_ = nullptr;
    }
}

inline void FluxSocket::postOpen()
{
    if (!onOpen)
        return;
    auto cb = onOpen;
    fluxPostToUIThread([cb](HttpResult)
                       { cb(); }, {});
}
inline void FluxSocket::postMessage(std::string msg)
{
    if (!onMessage)
        return;
    auto cb = onMessage;
    HttpResult r;
    r.body = std::move(msg);
    fluxPostToUIThread([cb](HttpResult r)
                       { cb(std::move(r.body)); }, std::move(r));
}
inline void FluxSocket::postError(std::string err)
{
    if (!onError)
        return;
    auto cb = onError;
    HttpResult r;
    r.error = std::move(err);
    fluxPostToUIThread([cb](HttpResult r)
                       { cb(std::move(r.error)); }, std::move(r));
}
inline void FluxSocket::postClose(int code)
{
    if (!onClose)
        return;
    auto cb = onClose;
    HttpResult r;
    r.statusCode = code;
    fluxPostToUIThread([cb](HttpResult r)
                       { cb(r.statusCode); }, std::move(r));
}

#endif // !__EMSCRIPTEN__