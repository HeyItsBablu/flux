// app.cpp — Audio Player
// All playback state lives inside AudioPlayerWidget. app.cpp is just a host.
#include "app.hpp"
#include "flux/flux.hpp"


WidgetPtr createApp(FluxUI* app) {
    auto player = AudioPlayer("audio/sample.mp3")
            ->setWidth(380);

    return FluxApp(
            "FluxUI – Audio",
            Center(player),
            AppTheme::light(),
            false, 420, 200,
            false, false
    );
}