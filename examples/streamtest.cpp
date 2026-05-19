#include "flux/flux.hpp"

#include <iostream>

class EchoTestApp : public Widget {
public:
    WidgetPtr build() override {



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