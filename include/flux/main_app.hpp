#pragma once
#include "flux/flux.hpp"

struct AppConfig {
    const char *title      = "FluxUI App";
    int         width      = 900;
    int         height     = 700;
    bool        maximize   = false;
    bool        fullscreen = true;  // uses screen resolution
};

AppConfig  appConfig();
WidgetPtr  createApp(FluxUI *app);