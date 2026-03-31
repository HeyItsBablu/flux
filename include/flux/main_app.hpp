// app.hpp
#pragma once

#include "flux/flux.hpp"



struct AppConfig {
    const char* title      = "FluxUI App";
    int         width      = 900;
    int         height     = 700;
    bool        maximize   = false;
    bool        fullscreen = false;
};

// ── Implement both of these in your app.cpp ───────────────────────────────────

// Returns window configuration read by main() before createWindow() is called.
AppConfig appConfig();

// Builds and returns the root widget tree.
WidgetPtr createApp(FluxUI* app);