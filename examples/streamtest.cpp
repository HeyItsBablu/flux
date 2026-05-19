#include "flux/flux.hpp"

#include <curl/curl.h>
#include <iostream>

class EchoTestApp : public Widget {
public:
    WidgetPtr build() override {

        // ── Print curl version info at startup ─────────────────────────────
        curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);
        std::cout << "=== CURL VERSION INFO ===" << std::endl;
        std::cout << "curl version : " << info->version << std::endl;
        std::cout << "ssl version  : " << (info->ssl_version ? info->ssl_version : "none") << std::endl;

        // Check WebSocket feature flag
        bool hasWS = (info->features & CURL_VERSION_HTTP2) != 0;
        std::cout << "features     : 0x" << std::hex << info->features << std::dec << std::endl;

        // CURL_VERSION_WEBSOCKETS was added in 7.86.0
        #ifdef CURL_VERSION_WEBSOCKETS
            bool wsFlag = (info->features & CURL_VERSION_WEBSOCKETS) != 0;
            std::cout << "WebSocket    : " << (wsFlag ? "YES" : "NO") << std::endl;
        #else
            std::cout << "WebSocket    : CURL_VERSION_WEBSOCKETS not defined — curl too old or not rebuilt" << std::endl;
        #endif

        // Print supported protocols
        std::cout << "protocols    : ";
        for (int i = 0; info->protocols[i]; ++i)
            std::cout << info->protocols[i] << " ";
        std::cout << std::endl;
        std::cout << "=========================" << std::endl;

        return Scaffold(
            AppBar("WebSocket Echo Test"),
            Expanded(
                StreamBuilder(
                    "wss://echo.websocket.org",
                    [](const StreamSnapshot<std::string>& snap) -> WidgetPtr {

                        std::cout << "[StreamBuilder] state=";
                        switch (snap.state) {
                            case StreamState::None:       std::cout << "None";       break;
                            case StreamState::Connecting: std::cout << "Connecting"; break;
                            case StreamState::Active:     std::cout << "Active";     break;
                            case StreamState::Done:       std::cout << "Done";       break;
                            case StreamState::Error:      std::cout << "Error: " << snap.error; break;
                        }
                        std::cout << std::endl;

                        if (snap.isConnecting())
                            return Center(Text("Connecting..."));
                        if (snap.hasError())
                            return Center(Text("Error: " + snap.error));
                        if (snap.isDone())
                            return Center(Text("Connection closed."));
                        if (!snap.hasData())
                            return Center(Text("Connected. Waiting..."));

                        return Center(Text("Received: " + snap.data));
                    }
                )
            ),nullptr,
            nullptr
        );
    }
};

WidgetPtr createApp(FluxUI* app) {
    return FluxApp(
        "Echo Test",
        std::make_shared<EchoTestApp>(),
        AppTheme::light(),
        false, 500, 300, false, false
    );
}