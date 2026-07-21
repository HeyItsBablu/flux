#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// ============================================================================
// FLUX WEBRTC
//
// Mirrors the FluxHttp / FluxSocket split: a single public API in this
// header, two mutually-exclusive backends selected at compile time —
//
//   native (non-Emscripten): src/flux_webrtc_native.cpp, built on
//     libdatachannel (external/libdatachannel submodule, same pattern as
//     external/curl). Handles SDP/ICE/DTLS/SCTP plumbing; libdatachannel
//     does NOT capture or encode audio/video itself — see FluxMediaSource
//     below for where that plugs in.
//
//   web (Emscripten): src/flux_webrtc_web.cpp, thin glue around the
//     browser's own RTCPeerConnection/getUserMedia — no library needed,
//     the browser IS the WebRTC stack. Matches file naming (*_web.cpp) so
//     CMakeLists.txt's existing include/exclude filters pick it up for
//     free, same as flux_http_web.cpp.
//
// SIGNALING (SDP offer/answer + ICE candidate exchange) is deliberately
// NOT part of this class — same philosophy as FluxSocket not owning your
// app protocol. See flux_webrtc_signaling.hpp for a ready-made client
// built on FluxSocket + your JSON layer, or wire your own: forward
// whatever this class hands you via onLocalDescription/onIceCandidate to
// the remote peer over any channel, and feed whatever arrives back in via
// setRemoteDescription/addRemoteIceCandidate.
// ============================================================================

struct FluxIceServer
{
    std::string urls;       // e.g. "stun:stun.l.google.com:19302" or "turn:host:3478"
    std::string username;   // TURN only
    std::string credential; // TURN only
};

struct FluxRtcConfig
{
    std::vector<FluxIceServer> iceServers = {
        {"stun:stun.l.google.com:19302", "", ""}};
};

enum class FluxRtcConnectionState
{
    New,
    Connecting,
    Connected,
    Disconnected,
    Failed,
    Closed
};

enum class FluxRtcMediaKind
{
    Audio,
    Video
};

// ============================================================================
// DATA CHANNEL
// ============================================================================

class FluxDataChannel
{
public:
    std::function<void()> onOpen;
    std::function<void(std::string)> onMessage; // text frame
    std::function<void(std::vector<uint8_t>)> onBinaryMessage;
    std::function<void()> onClose;

    virtual ~FluxDataChannel() = default;
    virtual void send(const std::string &text) = 0;
    virtual void sendBinary(const std::vector<uint8_t> &data) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual const std::string &label() const = 0;
};
using FluxDataChannelPtr = std::shared_ptr<FluxDataChannel>;

// ============================================================================
// MEDIA
//
// Deliberately push/pull-based and dumb about capture: this layer does no
// camera/mic acquisition itself.
//   - Remote tracks (FluxMediaTrack): native hands you decoded frames via
//     onVideoFrame/onAudioFrame so you can feed them into your own painter
//     (e.g. the same path VideoPlayerWidget already uses). Web instead
//     drives a real DOM <video> element directly through IDomAdapter —
//     matching the "no hand-painted media on web" rule flux_widget.hpp
//     already documents for text inputs — so onVideoFrame/onAudioFrame are
//     simply never invoked on the web backend; render via the DOM node.
//   - Local tracks (FluxMediaSource): something platform-specific (a
//     camera-capture helper, or FFmpeg's device demuxers already linked
//     into `flux` on native Linux; getUserMedia() on web) calls
//     pushVideoFrame/pushAudioFrame as frames become available.
// ============================================================================

struct FluxVideoFrame
{
    int width = 0, height = 0;
    std::vector<uint8_t> rgba; // already converted for painter consumption
};

struct FluxAudioFrame
{
    int sampleRate = 0, channels = 0;
    std::vector<float> samples; // interleaved
};

class FluxMediaTrack
{
public:
    std::function<void(FluxVideoFrame)> onVideoFrame;
    std::function<void(FluxAudioFrame)> onAudioFrame;

    virtual ~FluxMediaTrack() = default;
    virtual FluxRtcMediaKind kind() const = 0;
    virtual bool isRemote() const = 0;
};
using FluxMediaTrackPtr = std::shared_ptr<FluxMediaTrack>;

class FluxMediaSource
{
public:
    virtual ~FluxMediaSource() = default;
    virtual FluxRtcMediaKind kind() const = 0;
    virtual void pushVideoFrame(const FluxVideoFrame &) {}
    virtual void pushAudioFrame(const FluxAudioFrame &) {}
};
using FluxMediaSourcePtr = std::shared_ptr<FluxMediaSource>;

// ============================================================================
// PEER CONNECTION
// ============================================================================

class FluxPeerConnection : public std::enable_shared_from_this<FluxPeerConnection>
{
public:
    // Fired when a local SDP offer/answer is ready — forward it to the
    // remote peer over YOUR signaling channel. type is "offer" or "answer".
    std::function<void(std::string type, std::string sdp)> onLocalDescription;

    // Fired once per discovered local ICE candidate — forward each one to
    // the remote peer as it arrives (trickle ICE), don't batch them.
    std::function<void(std::string candidate, std::string sdpMid, int sdpMLineIndex)> onIceCandidate;

    std::function<void(FluxRtcConnectionState)> onConnectionStateChange;

    // Remote peer opened a data channel (you didn't call createDataChannel
    // for this one — they did, on their side).
    std::function<void(FluxDataChannelPtr)> onDataChannel;

    // Remote peer's media track arrived, after negotiation completes.
    std::function<void(FluxMediaTrackPtr)> onTrack;

    static std::shared_ptr<FluxPeerConnection> create(const FluxRtcConfig &config);
    virtual ~FluxPeerConnection() = default;

    // ── Outgoing negotiation ────────────────────────────────────────────
    // Call on the side that starts the call. Result arrives via onLocalDescription.
    virtual void createOffer() = 0;
    // Call on the side that received an offer, after setRemoteDescription.
    virtual void createAnswer() = 0;

    // ── Feed in what arrived over your signaling channel ────────────────
    virtual void setRemoteDescription(const std::string &type, const std::string &sdp) = 0;
    virtual void addRemoteIceCandidate(const std::string &candidate,
                                       const std::string &sdpMid, int sdpMLineIndex) = 0;

    // ── Data channels ────────────────────────────────────────────────────
    // Only the side that calls this before createOffer() needs to; the
    // other side receives the channel via onDataChannel.
    virtual FluxDataChannelPtr createDataChannel(const std::string &label) = 0;

    // ── Media ────────────────────────────────────────────────────────────
    virtual void addLocalTrack(FluxMediaSourcePtr source) = 0;

    virtual void close() = 0;
    virtual FluxRtcConnectionState state() const = 0;
};
using FluxPeerConnectionPtr = std::shared_ptr<FluxPeerConnection>;