// flux_webrtc_native.cpp
// Native only (Windows/Linux/macOS/Android). Built on libdatachannel —
// external/libdatachannel, added the same way external/curl is: as a git
// submodule pulled in via add_subdirectory() in CMakeLists.txt.
//
// Why libdatachannel and not Google's libwebrtc: libwebrtc is enormous
// (hours-long builds, its own toolchain, ~1-2GB of build artifacts) and
// wildly impractical to vendor as a submodule next to curl/mbedtls the way
// this project vendors everything else. libdatachannel is a small,
// permissively-licensed C/C++ library that implements the same
// ICE/DTLS/SCTP/SRTP plumbing on top of libjuice + usrsctp + OpenSSL (or
// mbedTLS on Android, matching the existing curl config), builds in
// seconds, and is what several other lightweight native app frameworks
// already use for exactly this reason.
//
// libdatachannel handles data channels end-to-end. For audio/video it
// gives you raw RTP in/out (rtc::Track) — it does NOT capture, encode, or
// decode media itself. addLocalTrack()/onTrack() below are wired as far as
// the RTP plumbing goes; hooking in an actual encoder/decoder (e.g. via
// libavcodec, already linked into `flux` on native Linux for
// VideoPlayerWidget) is left as a follow-up — see the TODOs.

#ifndef __EMSCRIPTEN__

#include "flux/flux_webrtc.hpp"
#include <rtc/rtc.hpp>

#include <mutex>
#include <unordered_map>

namespace
{

// ============================================================================
// DATA CHANNEL
// ============================================================================

class NativeDataChannel : public FluxDataChannel
{
public:
    explicit NativeDataChannel(std::shared_ptr<rtc::DataChannel> dc)
        : dc_(std::move(dc)), label_(dc_->label())
    {
        dc_->onOpen([this]
                   { if (onOpen) onOpen(); });
        dc_->onClosed([this]
                     { if (onClose) onClose(); });
        dc_->onMessage([this](rtc::message_variant data)
                      {
            if (std::holds_alternative<std::string>(data)) {
                if (onMessage) onMessage(std::get<std::string>(data));
            } else {
                auto &bin = std::get<rtc::binary>(data);
                if (onBinaryMessage) {
                    onBinaryMessage(std::vector<uint8_t>(
                        reinterpret_cast<const uint8_t *>(bin.data()),
                        reinterpret_cast<const uint8_t *>(bin.data()) + bin.size()));
                }
            } });
    }

    void send(const std::string &text) override
    {
        if (dc_->isOpen())
            dc_->send(text);
    }
    void sendBinary(const std::vector<uint8_t> &data) override
    {
        if (!dc_->isOpen())
            return;
        dc_->send(reinterpret_cast<const std::byte *>(data.data()), data.size());
    }
    void close() override { dc_->close(); }
    bool isOpen() const override { return dc_->isOpen(); }
    const std::string &label() const override { return label_; }

private:
    std::shared_ptr<rtc::DataChannel> dc_;
    std::string label_;
};

// ============================================================================
// PEER CONNECTION
// ============================================================================

class NativePeerConnection : public FluxPeerConnection
{
public:
    explicit NativePeerConnection(const FluxRtcConfig &config)
    {
        rtc::Configuration cfg;
        for (auto &s : config.iceServers)
        {
            rtc::IceServer ice(s.urls);
            if (!s.username.empty())
            {
                ice.username = s.username;
                ice.password = s.credential;
            }
            cfg.iceServers.push_back(ice);
        }
        pc_ = std::make_shared<rtc::PeerConnection>(cfg);

        pc_->onLocalDescription([this](rtc::Description desc)
                               {
            if (onLocalDescription)
                onLocalDescription(desc.typeString(), std::string(desc)); });

        pc_->onLocalCandidate([this](rtc::Candidate cand)
                             {
            if (onIceCandidate)
                onIceCandidate(std::string(cand), cand.mid(), 0); });

        pc_->onStateChange([this](rtc::PeerConnection::State s)
                          {
            state_ = mapState(s);
            if (onConnectionStateChange)
                onConnectionStateChange(state_); });

        pc_->onDataChannel([this](std::shared_ptr<rtc::DataChannel> dc)
                          {
            if (onDataChannel)
                onDataChannel(std::make_shared<NativeDataChannel>(dc)); });

        // TODO: pc_->onTrack(...) — wire remote RTP into a decoder
        // (libavcodec) and forward decoded frames via
        // FluxMediaTrack::onVideoFrame/onAudioFrame. Skipped for now: needs
        // per-codec (H264/VP8/Opus) depacketization + decode, which is a
        // separate chunk of work from the connection plumbing here.
    }

    void createOffer() override { pc_->setLocalDescription(); }
    void createAnswer() override { pc_->setLocalDescription(); }

    void setRemoteDescription(const std::string &type, const std::string &sdp) override
    {
        pc_->setRemoteDescription(rtc::Description(sdp, type));
    }
    void addRemoteIceCandidate(const std::string &candidate,
                               const std::string &sdpMid, int /*idx*/) override
    {
        pc_->addRemoteCandidate(rtc::Candidate(candidate, sdpMid));
    }

    FluxDataChannelPtr createDataChannel(const std::string &label) override
    {
        auto dc = pc_->createDataChannel(label);
        return std::make_shared<NativeDataChannel>(dc);
    }

    void addLocalTrack(FluxMediaSourcePtr /*source*/) override
    {
        // TODO: build an rtc::Description::Media + rtc::Track with the
        // right codec, then have FluxMediaSource::pushVideoFrame/
        // pushAudioFrame feed an encoder (libavcodec) whose output packets
        // get RTP-packetized and sent via the track. Needs an encoder,
        // which this pass does not include — see file header.
    }

    void close() override { pc_->close(); }
    FluxRtcConnectionState state() const override { return state_; }

private:
    static FluxRtcConnectionState mapState(rtc::PeerConnection::State s)
    {
        using S = rtc::PeerConnection::State;
        switch (s)
        {
        case S::New: return FluxRtcConnectionState::New;
        case S::Connecting: return FluxRtcConnectionState::Connecting;
        case S::Connected: return FluxRtcConnectionState::Connected;
        case S::Disconnected: return FluxRtcConnectionState::Disconnected;
        case S::Failed: return FluxRtcConnectionState::Failed;
        case S::Closed: return FluxRtcConnectionState::Closed;
        }
        return FluxRtcConnectionState::New;
    }

    std::shared_ptr<rtc::PeerConnection> pc_;
    FluxRtcConnectionState state_ = FluxRtcConnectionState::New;
};

} // namespace

std::shared_ptr<FluxPeerConnection> FluxPeerConnection::create(const FluxRtcConfig &config)
{
    return std::make_shared<NativePeerConnection>(config);
}

#endif // !__EMSCRIPTEN__