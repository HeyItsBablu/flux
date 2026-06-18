// src/flux_socket_web.cpp
// Emscripten only — compiled instead of the libcurl path.

#ifdef __EMSCRIPTEN__

#include "flux/flux_socket.hpp"

#include <emscripten.h>
#include <cassert>
#include <unordered_map>
#include <mutex>

// ============================================================================
// JS-side socket table
//
// We keep a C-side map of  handle → FluxSocket*  so the JS callbacks
// (which only carry an integer handle) can find the right object.
// All accesses happen on the main thread, so a plain map is fine.
// ============================================================================

namespace
{
    int s_nextHandle = 1;
    std::unordered_map<int, FluxSocket *> s_sockets;
}

// ── Registration helpers called from connect() / close() ────────────────────

static int registerSocket(FluxSocket *s)
{
    int h = s_nextHandle++;
    s_sockets[h] = s;
    return h;
}

static void unregisterSocket(int h)
{
    s_sockets.erase(h);
}

static FluxSocket *findSocket(int h)
{
    auto it = s_sockets.find(h);
    return (it != s_sockets.end()) ? it->second : nullptr;
}

// ============================================================================
// C callbacks — called from JS via Module.ccall / dynCall.
// They run on the main thread so we can call fluxDrainHttpQueue-style
// posting; here we just invoke the C++ callbacks directly (already main thread).
// ============================================================================

extern "C"
{

    EMSCRIPTEN_KEEPALIVE
    void fluxWsOnOpen(int handle)
    {
        FluxSocket *s = findSocket(handle);
        if (!s)
            return;
        // Mark connected via the public accessor — we'll use a small friend trick below.
        // For now just fire the callback; connected_ is set in connect() after JS open.
        if (s->onOpen)
            s->onOpen();
    }

    EMSCRIPTEN_KEEPALIVE
    void fluxWsOnMessage(int handle, const char *data, int length)
    {
        FluxSocket *s = findSocket(handle);
        if (!s || !s->onMessage)
            return;
        s->onMessage(std::string(data, length));
    }

    EMSCRIPTEN_KEEPALIVE
    void fluxWsOnError(int handle, const char *msg)
    {
        FluxSocket *s = findSocket(handle);
        if (!s || !s->onError)
            return;
        s->onError(std::string(msg));
    }

    EMSCRIPTEN_KEEPALIVE
    void fluxWsOnClose(int handle, int code)
    {
        FluxSocket *s = findSocket(handle);
        if (!s)
            return;
        unregisterSocket(handle);
        if (s->onClose)
            s->onClose(code);
    }

} // extern "C"

// ============================================================================
// FluxSocket — Emscripten implementation
// ============================================================================

void FluxSocket::connect(const std::string &url)
{
    close();

    wsHandle_ = registerSocket(this);
    connected_ = false;

    EM_ASM({
        var handle = $0;
        var url    = UTF8ToString($1);

        var ws = new WebSocket(url);
        ws.binaryType = 'arraybuffer';

        if (!Module._fluxSockets) Module._fluxSockets = {};
        Module._fluxSockets[handle] = ws;

        ws.onopen = function() {
            Module.ccall('fluxWsOnOpen', null, ['number'], [handle]);
        };

ws.onmessage = function(evt) {
    var text = (typeof evt.data === 'string')
        ? evt.data
        : new TextDecoder().decode(new Uint8Array(evt.data));
    var buf = stringToNewUTF8(text);
    var len = lengthBytesUTF8(text);
    Module.ccall('fluxWsOnMessage', null,
        ['number', 'number', 'number'], [handle, buf, len]);
    _free(buf);
};

ws.onerror = function() {
    var buf = stringToNewUTF8('WebSocket error');
    Module.ccall('fluxWsOnError', null, ['number', 'number'], [handle, buf]);
    _free(buf);
};

        ws.onclose = function(evt) {
            delete Module._fluxSockets[handle];
            Module.ccall('fluxWsOnClose', null,
                ['number', 'number'], [handle, evt.code]);
        }; }, wsHandle_, url.c_str());

    connected_ = true;
}

void FluxSocket::send(const std::string &message)
{
    if (!connected_ || wsHandle_ < 0)
        return;
    EM_ASM({
        var handle = $0;
        var msg    = UTF8ToString($1);
        var ws     = Module._fluxSockets && Module._fluxSockets[handle];
        if (ws && ws.readyState === WebSocket.OPEN)
            ws.send(msg); }, wsHandle_, message.c_str());
}

void FluxSocket::close()
{
    if (wsHandle_ < 0)
        return;

    EM_ASM({
        var handle = $0;
        var ws     = Module._fluxSockets && Module._fluxSockets[handle];
        if (ws) {
            ws.onopen = ws.onmessage = ws.onerror = ws.onclose = null;
            if (ws.readyState < 2) ws.close(1000, 'client close');
            delete Module._fluxSockets[handle];
        } }, wsHandle_);

    unregisterSocket(wsHandle_);
    wsHandle_ = -1;
    connected_ = false;
}

#endif // __EMSCRIPTEN__