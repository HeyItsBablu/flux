// flux_webrtc_web.cpp
// Emscripten-only. Named *_web.cpp on purpose so CMakeLists.txt's existing
// filters (list(FILTER FLUX_SOURCES EXCLUDE REGEX ".*_web\\.cpp$") on
// non-web builds) already do the right thing with zero CMake changes,
// same as flux_http_web.cpp.
//
// No library needed here: the browser IS the WebRTC stack. This file is a
// thin id-based bridge between C++ objects and real JS-side
// RTCPeerConnection/RTCDataChannel instances — same shape as
// flux_http_web.cpp bridging to the Fetch API.

#ifdef __EMSCRIPTEN__

#include "flux/flux_webrtc.hpp"

#include <emscripten.h>
#include <emscripten/val.h>

#include <mutex>
#include <unordered_map>

// ============================================================================
// Forward declarations of the C++ wrapper classes, so the registry maps
// below can reference them before their full definitions further down.
// ============================================================================

class WebDataChannel;
class WebPeerConnection;

namespace
{
    std::mutex gRegistryMutex;
    std::unordered_map<int, WebPeerConnection *> gPcRegistry;
    std::unordered_map<int, WebDataChannel *> gDcRegistry;
} // namespace

// ============================================================================
// JS-side object table + glue.
//
// Module.__fluxRtc.pcs / .dcs hold the real RTCPeerConnection /
// RTCDataChannel objects, keyed by an integer id the C++ side owns.
// Every event crosses back into C++ via Module.ccall into one of the
// flux_rtc_on_*/flux_rtc_dc_on_* trampolines defined below.
// ============================================================================

EM_JS(int, flux_rtc_create, (const char *iceServersJson), {
    if (!Module.__fluxRtc)
        Module.__fluxRtc = {pcs : {}, dcs : {}, nextId : 1};

    var cfg = JSON.parse(UTF8ToString(iceServersJson));
    var pc = new RTCPeerConnection({iceServers : cfg});
    var id = Module.__fluxRtc.nextId++;
    Module.__fluxRtc.pcs[id] = pc;

    pc.onicecandidate = function(e) {
        if (!e.candidate)
            return;
        Module.ccall('flux_rtc_on_ice_candidate', null,
                     ['number', 'string', 'string', 'number'],
                     [id, e.candidate.candidate, e.candidate.sdpMid || "",
                      e.candidate.sdpMLineIndex || 0]);
    };
    pc.onconnectionstatechange = function() {
        Module.ccall('flux_rtc_on_state_change', null, ['number', 'string'],
                     [id, pc.connectionState]);
    };
    pc.ondatachannel = function(e) {
        var dcId = Module.__fluxRtc.nextId++;
        var channel = e.channel;
        Module.__fluxRtc.dcs[dcId] = channel;

        // Inlined instead of calling out to a shared helper: EM_JS blocks
        // compile to independent JS snippets and can't reliably call each
        // other by their wrapped C name, so the wiring logic below is
        // duplicated here and in flux_rtc_create_datachannel further down.
        channel.binaryType = "arraybuffer";
        channel.onopen = function() {
            Module.ccall('flux_rtc_dc_on_open', null, [ 'number' ], [ dcId ]);
        };
        channel.onclose = function() {
            Module.ccall('flux_rtc_dc_on_close', null, [ 'number' ], [ dcId ]);
        };
        channel.onmessage = function(ev) {
            if (typeof ev.data === "string") {
                Module.ccall('flux_rtc_dc_on_message', null,
                             ['number', 'string'], [dcId, ev.data]);
            } else {
                var bytes = new Uint8Array(ev.data);
                var ptr = Module._malloc(bytes.length);
                Module.HEAPU8.set(bytes, ptr);
                Module.ccall('flux_rtc_dc_on_binary', null,
                             ['number', 'number', 'number'],
                             [dcId, ptr, bytes.length]);
                Module._free(ptr);
            }
        };

        Module.ccall('flux_rtc_on_data_channel', null,
                     ['number', 'number', 'string'],
                     [id, dcId, channel.label]);
    };
    pc.ontrack = function(e) {
        // Rendering a remote track on web is expected to go through a real
        // DOM <video>/<audio> element via IDomAdapter (matching the
        // no-hand-painted-media rule elsewhere in this codebase), not
        // through this bridge's frame callbacks. This hook only announces
        // that a track arrived; wiring it to a DOM element is left as a
        // follow-up alongside addLocalTrack() below.
        Module.ccall('flux_rtc_on_track', null, ['number', 'string'],
                     [id, e.track.kind]);
    };
    return id;
});

EM_JS(int, flux_rtc_create_datachannel, (int pcId, const char *label), {
    var pc = Module.__fluxRtc.pcs[pcId];
    var channel = pc.createDataChannel(UTF8ToString(label));
    var dcId = Module.__fluxRtc.nextId++;
    Module.__fluxRtc.dcs[dcId] = channel;

    // Same wiring as the ondatachannel handler in flux_rtc_create above —
    // duplicated rather than shared; see the comment there for why.
    channel.binaryType = "arraybuffer";
    channel.onopen = function() {
        Module.ccall('flux_rtc_dc_on_open', null, [ 'number' ], [ dcId ]);
    };
    channel.onclose = function() {
        Module.ccall('flux_rtc_dc_on_close', null, [ 'number' ], [ dcId ]);
    };
    channel.onmessage = function(e) {
        if (typeof e.data === "string") {
            Module.ccall('flux_rtc_dc_on_message', null,
                         ['number', 'string'], [dcId, e.data]);
        } else {
            var bytes = new Uint8Array(e.data);
            var ptr = Module._malloc(bytes.length);
            Module.HEAPU8.set(bytes, ptr);
            Module.ccall('flux_rtc_dc_on_binary', null,
                         ['number', 'number', 'number'],
                         [dcId, ptr, bytes.length]);
            Module._free(ptr);
        }
    };
    return dcId;
});

EM_JS(void, flux_rtc_dc_send_text, (int dcId, const char *text), {
    Module.__fluxRtc.dcs[dcId].send(UTF8ToString(text));
});

EM_JS(void, flux_rtc_dc_send_binary, (int dcId, const uint8_t *ptr, int len), {
    Module.__fluxRtc.dcs[dcId].send(Module.HEAPU8.slice(ptr, ptr + len));
});

EM_JS(void, flux_rtc_dc_close, (int dcId), {
    Module.__fluxRtc.dcs[dcId].close();
});

EM_JS(void, flux_rtc_create_offer, (int pcId), {
    var pc = Module.__fluxRtc.pcs[pcId];
    pc.createOffer()
        .then(function(desc) { return pc.setLocalDescription(desc); })
        .then(function() {
            var d = pc.localDescription;
            Module.ccall('flux_rtc_on_local_description', null,
                         ['number', 'string', 'string'], [pcId, d.type, d.sdp]);
        });
});

EM_JS(void, flux_rtc_create_answer, (int pcId), {
    var pc = Module.__fluxRtc.pcs[pcId];
    pc.createAnswer()
        .then(function(desc) { return pc.setLocalDescription(desc); })
        .then(function() {
            var d = pc.localDescription;
            Module.ccall('flux_rtc_on_local_description', null,
                         ['number', 'string', 'string'], [pcId, d.type, d.sdp]);
        });
});

EM_JS(void, flux_rtc_set_remote_description,
     (int pcId, const char *type, const char *sdp), {
         var pc = Module.__fluxRtc.pcs[pcId];
         pc.setRemoteDescription(
             {type : UTF8ToString(type), sdp : UTF8ToString(sdp)});
     });

EM_JS(void, flux_rtc_add_ice_candidate,
     (int pcId, const char *candidate, const char *sdpMid, int idx), {
         var pc = Module.__fluxRtc.pcs[pcId];
         pc.addIceCandidate({
             candidate : UTF8ToString(candidate),
             sdpMid : UTF8ToString(sdpMid),
             sdpMLineIndex : idx
         });
     });

EM_JS(void, flux_rtc_close, (int pcId), {
    Module.__fluxRtc.pcs[pcId].close();
    delete Module.__fluxRtc.pcs[pcId];
});

// ============================================================================
// Trampoline declarations — implementations are below the wrapper classes,
// since they need to reach into the registries to dispatch.
// ============================================================================

extern "C"
{
    EMSCRIPTEN_KEEPALIVE void flux_rtc_on_ice_candidate(int pcId, const char *candidate, const char *sdpMid, int idx);
    EMSCRIPTEN_KEEPALIVE void flux_rtc_on_state_change(int pcId, const char *state);
    EMSCRIPTEN_KEEPALIVE void flux_rtc_on_data_channel(int pcId, int dcId, const char *label);
    EMSCRIPTEN_KEEPALIVE void flux_rtc_on_track(int pcId, const char *kind);
    EMSCRIPTEN_KEEPALIVE void flux_rtc_on_local_description(int pcId, const char *type, const char *sdp);
    EMSCRIPTEN_KEEPALIVE void flux_rtc_dc_on_open(int dcId);
    EMSCRIPTEN_KEEPALIVE void flux_rtc_dc_on_close(int dcId);
    EMSCRIPTEN_KEEPALIVE void flux_rtc_dc_on_message(int dcId, const char *text);
    EMSCRIPTEN_KEEPALIVE void flux_rtc_dc_on_binary(int dcId, const uint8_t *ptr, int len);
}

// ============================================================================
// C++ WRAPPERS
// ============================================================================

class WebDataChannel : public FluxDataChannel
{
public:
    WebDataChannel(int dcId, std::string label) : dcId_(dcId), label_(std::move(label))
    {
        std::lock_guard<std::mutex> lock(gRegistryMutex);
        gDcRegistry[dcId_] = this;
    }
    ~WebDataChannel() override
    {
        std::lock_guard<std::mutex> lock(gRegistryMutex);
        gDcRegistry.erase(dcId_);
    }

    void send(const std::string &text) override { flux_rtc_dc_send_text(dcId_, text.c_str()); }
    void sendBinary(const std::vector<uint8_t> &data) override
    {
        flux_rtc_dc_send_binary(dcId_, data.data(), static_cast<int>(data.size()));
    }
    void close() override { flux_rtc_dc_close(dcId_); }
    bool isOpen() const override { return open_; }
    const std::string &label() const override { return label_; }

    // Dispatched into from the trampolines at the bottom of this file.
    void handleOpen() { open_ = true; if (onOpen) onOpen(); }
    void handleClose() { open_ = false; if (onClose) onClose(); }
    void handleMessage(const std::string &text) { if (onMessage) onMessage(text); }
    void handleBinary(std::vector<uint8_t> data) { if (onBinaryMessage) onBinaryMessage(std::move(data)); }

private:
    int dcId_;
    std::string label_;
    bool open_ = false;
};

class WebPeerConnection : public FluxPeerConnection
{
public:
    explicit WebPeerConnection(const FluxRtcConfig &config)
    {
        // Minimal hand-rolled JSON array literal — avoids pulling
        // flux_json.hpp into this leaf translation unit for a one-line
        // string-array encode.
        std::string json = "[";
        for (size_t i = 0; i < config.iceServers.size(); ++i)
        {
            if (i)
                json += ",";
            json += "\"" + config.iceServers[i].urls + "\"";
        }
        json += "]";

        pcId_ = flux_rtc_create(json.c_str());
        std::lock_guard<std::mutex> lock(gRegistryMutex);
        gPcRegistry[pcId_] = this;
    }
    ~WebPeerConnection() override
    {
        {
            std::lock_guard<std::mutex> lock(gRegistryMutex);
            gPcRegistry.erase(pcId_);
        }
        flux_rtc_close(pcId_);
    }

    void createOffer() override { flux_rtc_create_offer(pcId_); }
    void createAnswer() override { flux_rtc_create_answer(pcId_); }

    void setRemoteDescription(const std::string &type, const std::string &sdp) override
    {
        flux_rtc_set_remote_description(pcId_, type.c_str(), sdp.c_str());
    }
    void addRemoteIceCandidate(const std::string &candidate,
                               const std::string &sdpMid, int idx) override
    {
        flux_rtc_add_ice_candidate(pcId_, candidate.c_str(), sdpMid.c_str(), idx);
    }
    FluxDataChannelPtr createDataChannel(const std::string &label) override
    {
        int dcId = flux_rtc_create_datachannel(pcId_, label.c_str());
        return std::make_shared<WebDataChannel>(dcId, label);
    }
    void addLocalTrack(FluxMediaSourcePtr /*source*/) override
    {
        // TODO: bridge to getUserMedia()/pc.addTrack() — needs a JS-side
        // MediaStream table analogous to Module.__fluxRtc.dcs above, plus
        // a device-picker UI. Left for a follow-up pass; see file header.
    }
    void close() override { flux_rtc_close(pcId_); }
    FluxRtcConnectionState state() const override { return state_; }

    // Dispatched into from the trampolines below.
    void handleLocalDescription(const std::string &type, const std::string &sdp)
    {
        if (onLocalDescription) onLocalDescription(type, sdp);
    }
    void handleIceCandidate(const std::string &c, const std::string &mid, int idx)
    {
        if (onIceCandidate) onIceCandidate(c, mid, idx);
    }
    void handleStateChange(const std::string &s)
    {
        if (s == "new") state_ = FluxRtcConnectionState::New;
        else if (s == "connecting") state_ = FluxRtcConnectionState::Connecting;
        else if (s == "connected") state_ = FluxRtcConnectionState::Connected;
        else if (s == "disconnected") state_ = FluxRtcConnectionState::Disconnected;
        else if (s == "failed") state_ = FluxRtcConnectionState::Failed;
        else if (s == "closed") state_ = FluxRtcConnectionState::Closed;
        if (onConnectionStateChange) onConnectionStateChange(state_);
    }
    void handleDataChannel(int dcId, const std::string &label)
    {
        auto dc = std::make_shared<WebDataChannel>(dcId, label);
        if (onDataChannel) onDataChannel(dc);
    }

private:
    int pcId_ = 0;
    FluxRtcConnectionState state_ = FluxRtcConnectionState::New;
};

std::shared_ptr<FluxPeerConnection> FluxPeerConnection::create(const FluxRtcConfig &config)
{
    return std::make_shared<WebPeerConnection>(config);
}

// ============================================================================
// TRAMPOLINE DEFINITIONS — route JS callbacks to the right C++ object
// ============================================================================

extern "C"
{
    void flux_rtc_on_ice_candidate(int pcId, const char *candidate, const char *sdpMid, int idx)
    {
        std::lock_guard<std::mutex> lock(gRegistryMutex);
        auto it = gPcRegistry.find(pcId);
        if (it != gPcRegistry.end()) it->second->handleIceCandidate(candidate, sdpMid, idx);
    }
    void flux_rtc_on_state_change(int pcId, const char *state)
    {
        std::lock_guard<std::mutex> lock(gRegistryMutex);
        auto it = gPcRegistry.find(pcId);
        if (it != gPcRegistry.end()) it->second->handleStateChange(state);
    }
    void flux_rtc_on_data_channel(int pcId, int dcId, const char *label)
    {
        std::lock_guard<std::mutex> lock(gRegistryMutex);
        auto it = gPcRegistry.find(pcId);
        if (it != gPcRegistry.end()) it->second->handleDataChannel(dcId, label);
    }
    void flux_rtc_on_track(int /*pcId*/, const char * /*kind*/)
    {
        // Hook point for a future onTrack→DOM bridge (see addLocalTrack TODO).
    }
    void flux_rtc_on_local_description(int pcId, const char *type, const char *sdp)
    {
        std::lock_guard<std::mutex> lock(gRegistryMutex);
        auto it = gPcRegistry.find(pcId);
        if (it != gPcRegistry.end()) it->second->handleLocalDescription(type, sdp);
    }
    void flux_rtc_dc_on_open(int dcId)
    {
        std::lock_guard<std::mutex> lock(gRegistryMutex);
        auto it = gDcRegistry.find(dcId);
        if (it != gDcRegistry.end()) it->second->handleOpen();
    }
    void flux_rtc_dc_on_close(int dcId)
    {
        std::lock_guard<std::mutex> lock(gRegistryMutex);
        auto it = gDcRegistry.find(dcId);
        if (it != gDcRegistry.end()) it->second->handleClose();
    }
    void flux_rtc_dc_on_message(int dcId, const char *text)
    {
        std::lock_guard<std::mutex> lock(gRegistryMutex);
        auto it = gDcRegistry.find(dcId);
        if (it != gDcRegistry.end()) it->second->handleMessage(text);
    }
    void flux_rtc_dc_on_binary(int dcId, const uint8_t *ptr, int len)
    {
        std::lock_guard<std::mutex> lock(gRegistryMutex);
        auto it = gDcRegistry.find(dcId);
        if (it != gDcRegistry.end()) it->second->handleBinary(std::vector<uint8_t>(ptr, ptr + len));
    }
}

#endif // __EMSCRIPTEN__