// windows/main.cpp
#include "flux/flux.hpp"

#include "AppConfig.generated.h"
#ifdef _WIN32

// Forward declaration — defined in lib/main.cpp
WidgetPtr createApp(FluxUI *app);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{

#ifdef FLUX_DEBUG
    AllocConsole();
    FILE *con;
    freopen_s(&con, "CONOUT$", "w", stdout);
#endif

    FluxUI app(hInstance);

    app.build([&]()
              {

        auto result = createApp(&app);
        return result; });

    int w = FLUX_APP_WINDOW_WIDTH;
    int h = FLUX_APP_WINDOW_HEIGHT;
    bool fullscreen = static_cast<bool>(FLUX_APP_FULLSCREEN);
    bool maximize = static_cast<bool>(FLUX_APP_MAXIMIZE);

    if (maximize && !fullscreen)
    {
        w = GetSystemMetrics(SM_CXSCREEN);
        h = GetSystemMetrics(SM_CYSCREEN);
    }

    app.createWindow(FLUX_APP_NAME, w, h);

    if (fullscreen)
    {
        // True borderless fullscreen: strip title bar/border, size to the
        // full monitor rect (not just the work area, which excludes the
        // taskbar). SW_MAXIMIZE alone can't do this — it keeps window chrome.
        HWND hwnd = app.getWindow();
        LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
        style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX |
                   WS_MAXIMIZEBOX | WS_SYSMENU);
        SetWindowLongPtr(hwnd, GWL_STYLE, style);

        HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfo(hmon, &mi);

        SetWindowPos(hwnd, HWND_TOP,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_NOZORDER | SWP_FRAMECHANGED);
    }
    else if (maximize)
    {
        ShowWindow(app.getWindow(), SW_MAXIMIZE);
    }
    int result = app.run();

    return result;
}
#endif