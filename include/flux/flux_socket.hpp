#pragma once
#include "flux_http_platform.hpp" // fluxPostToUIThread

#include <atomic>
#include <functional>
#include <string>
#include <thread>

// ============================================================================
// FORWARD DECLARATION
// ============================================================================

// Minimal WebSocket client built on libcurl's ws:// / wss:// support
// The same FluxHttp curl dependency covers it.
//
// Platform note: fluxPostToUIThread() is used to marshal every callback back
// onto the UI thread, exactly as FluxHttp does for HTTP responses.

// ============================================================================
// FLUX SOCKET
// ============================================================================

class FluxSocket {
public:
    // Callbacks — always invoked on the UI thread.
    std::function<void()>                  onOpen;
    std::function<void(std::string)>       onMessage;  // text frame
    std::function<void(std::string)>       onError;
    std::function<void(int /*code*/)>      onClose;

    FluxSocket() = default;

    ~FluxSocket() { close(); }

    // Non-copyable, non-movable (owns a thread + shared state).
    FluxSocket(const FluxSocket &)            = delete;
    FluxSocket &operator=(const FluxSocket &) = delete;

    // ── Connect ──────────────────────────────────────────────────────────────
    // Spawns a background thread that drives the WebSocket receive loop.
    // url must start with ws:// or wss://.
    void connect(const std::string &url) {
        close(); // tear down any existing connection first

        running_   = true;
        connected_ = false;

        thread_ = std::thread([this, url]() { receiveLoop(url); });
    }

    // ── Send ─────────────────────────────────────────────────────────────────
    // Safe to call from the UI thread while connected.
    void send(const std::string &message) {
        if (!connected_) return;
        wsSend(message);
    }

    // ── Close ────────────────────────────────────────────────────────────────
    void close() {
        running_ = false;
        if (curl_) {
            // Sending a close frame wakes the receive loop so it can exit.
            curl_ws_send(curl_, nullptr, 0, nullptr, 0, CURLWS_CLOSE);
        }
        if (thread_.joinable())
            thread_.join();

        cleanupCurl();
        connected_ = false;
    }

    bool isConnected() const { return connected_; }

private:
    CURL            *curl_      = nullptr;
    std::thread      thread_;
    std::atomic<bool> running_  {false};
    std::atomic<bool> connected_{false};

    // ── libcurl WebSocket receive loop (background thread) ───────────────────
    void receiveLoop(const std::string &url) {
        curl_ = curl_easy_init();
        if (!curl_) {
            postError("curl_easy_init failed");
            return;
        }

        curl_easy_setopt(curl_, CURLOPT_URL,            url.c_str());
        curl_easy_setopt(curl_, CURLOPT_CONNECT_ONLY,   2L); // WebSocket mode
        curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);

        CURLcode cc = curl_easy_perform(curl_);
        if (cc != CURLE_OK) {
            postError(curl_easy_strerror(cc));
            cleanupCurl();
            return;
        }

        connected_ = true;
        postOpen();

  
        std::string fragmentBuf;
        while (running_) {
            char   buf[4096];
            size_t bytesRecv  = 0;
            const curl_ws_frame *meta = nullptr;

            cc = curl_ws_recv(curl_, buf, sizeof(buf), &bytesRecv, &meta);

            if (cc == CURLE_AGAIN) {

                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            if (cc != CURLE_OK) {
                if (running_)
                    postError(curl_easy_strerror(cc));
                break;
            }
            if (!meta) continue;

            if (meta->flags & CURLWS_CLOSE) {
                int code = 1000;
                if (bytesRecv >= 2)
                    code = ((unsigned char)buf[0] << 8) | (unsigned char)buf[1];
                postClose(code);
                break;
            }

            if (meta->flags & CURLWS_TEXT) {
                fragmentBuf.append(buf, bytesRecv);

                if (meta->bytesleft == 0) {
                    postMessage(std::move(fragmentBuf));
                    fragmentBuf.clear();
                }
            }

        }

        cleanupCurl();
    }

    // ── Send (called from UI thread while curl_ is alive) ────────────────────
    void wsSend(const std::string &msg) {
        if (!curl_) return;
        size_t sent = 0;
        curl_ws_send(curl_, msg.data(), msg.size(), &sent, 0, CURLWS_TEXT);
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────
    void cleanupCurl() {
        if (curl_) {
            curl_easy_cleanup(curl_);
            curl_ = nullptr;
        }
    }

    // ── UI-thread post helpers ────────────────────────────────────────────────
    void postOpen() {
        if (!onOpen) return;
        auto cb = onOpen;
        fluxPostToUIThread([cb](HttpResult) { cb(); }, {});
    }
    void postMessage(std::string msg) {
        if (!onMessage) return;
        auto cb = onMessage;
        // We piggy-back on HttpResult.body to carry the payload.
        HttpResult r; r.body = std::move(msg);
        fluxPostToUIThread([cb](HttpResult r) { cb(std::move(r.body)); },
                           std::move(r));
    }
    void postError(std::string err) {
        if (!onError) return;
        auto cb = onError;
        HttpResult r; r.error = std::move(err);
        fluxPostToUIThread([cb](HttpResult r) { cb(std::move(r.error)); },
                           std::move(r));
    }
    void postClose(int code) {
        if (!onClose) return;
        auto cb = onClose;
        HttpResult r; r.statusCode = code;
        fluxPostToUIThread([cb](HttpResult r) { cb(r.statusCode); },
                           std::move(r));
    }
};