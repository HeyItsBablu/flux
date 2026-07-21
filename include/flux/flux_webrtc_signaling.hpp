#pragma once
#include "flux_json.hpp"
#include "flux_socket.hpp"
#include "flux_webrtc.hpp"

#include <memory>
#include <sstream>
#include <string>

// ============================================================================
// FLUX WEBRTC SIGNALING
//
// Wires a FluxPeerConnection to a FluxSocket so two peers can exchange
// SDP/ICE without you hand-writing the glue every time.
//
// Wire protocol — plain JSON text frames over the existing FluxSocket:
//   {"type":"offer",  "sdp":"..."}
//   {"type":"answer", "sdp":"..."}
//   {"type":"ice","candidate":"...","sdpMid":"...","sdpMLineIndex":0}
//
// This assumes a signaling SERVER that just relays frames between exactly
// two clients in a "room" — that relay is NOT implemented here, only the
// client-side framing. Point it at any WS endpoint that does the relay (a
// few dozen lines on any backend using FluxSocket's server-side
// equivalent, or a plain Node/ws relay).
//
// Serialization here is hand-rolled on purpose: JsonValue's write-side API
// isn't part of the files this was written against, so encoding uses a
// small local escaper instead of guessing at a JsonWriter interface.
// Parsing uses JsonParser::tryParse, which IS used elsewhere in this
// codebase (see flux_futurebuilder.hpp) — swap encodeMessage() for your
// real JsonValue writer once you confirm its API.
// ============================================================================

namespace flux_webrtc_signaling_detail
{
    inline std::string jsonEscape(const std::string &s)
    {
        std::string out;
        out.reserve(s.size() + 8);
        for (char c : s)
        {
            switch (c)
            {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default: out += c; break;
            }
        }
        return out;
    }
}

class FluxWebRtcSignalingClient
{
public:
    FluxWebRtcSignalingClient(FluxPeerConnectionPtr pc, const std::string &signalingUrl)
        : pc_(std::move(pc))
    {
        socket_ = std::make_shared<FluxSocket>();

        pc_->onLocalDescription = [this](std::string type, std::string sdp)
        {
            using namespace flux_webrtc_signaling_detail;
            std::ostringstream oss;
            oss << "{\"type\":\"" << jsonEscape(type)
                << "\",\"sdp\":\"" << jsonEscape(sdp) << "\"}";
            socket_->send(oss.str());
        };

        pc_->onIceCandidate = [this](std::string candidate, std::string sdpMid, int idx)
        {
            using namespace flux_webrtc_signaling_detail;
            std::ostringstream oss;
            oss << "{\"type\":\"ice\",\"candidate\":\"" << jsonEscape(candidate)
                << "\",\"sdpMid\":\"" << jsonEscape(sdpMid)
                << "\",\"sdpMLineIndex\":" << idx << "}";
            socket_->send(oss.str());
        };

        socket_->onMessage = [this](std::string raw)
        {
            JsonValue msg;
            if (!JsonParser::tryParse(raw, msg))
                return;

            std::string type = msg["type"].getString();
            if (type == "offer")
            {
                pc_->setRemoteDescription("offer", msg["sdp"].getString());
                pc_->createAnswer();
            }
            else if (type == "answer")
            {
                pc_->setRemoteDescription("answer", msg["sdp"].getString());
            }
            else if (type == "ice")
            {
                pc_->addRemoteIceCandidate(msg["candidate"].getString(),
                                           msg["sdpMid"].getString(),
                                           static_cast<int>(msg["sdpMLineIndex"].getNumber()));
            }
        };

        socket_->onError = [this](std::string err)
        {
            if (onError)
                onError(err);
        };

        socket_->connect(signalingUrl);
    }

    // Call once connected, on whichever side should initiate the call.
    void startCall() { pc_->createOffer(); }

    void close() { socket_->close(); }

    std::function<void(std::string)> onError;

private:
    FluxPeerConnectionPtr pc_;
    std::shared_ptr<FluxSocket> socket_;
};