
#include "app.hpp"
#include "flux/flux.hpp"


WidgetPtr createApp(FluxUI* app) {
    auto player = VideoPlayer("videos/sample.mp4")
            ->setWidth(380)
            ->setHeight(270)    // 16:9
            ->setAutoPlay(false);

    return FluxApp(
            "FluxUI – Audio",
            Center(player),
            AppTheme::light(),
            false, 420, 200,
            false, false
    );
}