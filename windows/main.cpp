//windows/main.cpp
#include "flux/flux.hpp"
#include "flux/flux_debug_log.hpp"
#include "AppConfig.generated.h"
#ifdef _WIN32

// Forward declaration — defined in lib/main.cpp
WidgetPtr createApp(FluxUI* app);

#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // Open log file before anything else
    fopen_s(&getDebugLogFile(), "flux_debug.log", "w");

    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* ep) -> LONG {
        FILE* f = getDebugLogFile();
        if (f) { 
            fprintf(f, "\n[CRASH] Exception code: 0x%08X\n",
                    ep->ExceptionRecord->ExceptionCode);
            fprintf(f, "[CRASH] At address:      0x%p\n",
                    ep->ExceptionRecord->ExceptionAddress);

            // Symbolized stack walk
            SymInitialize(GetCurrentProcess(), nullptr, TRUE);

            CONTEXT ctx = *ep->ContextRecord;
            STACKFRAME64 sf = {};
            sf.AddrPC.Offset    = ctx.Rip;
            sf.AddrPC.Mode      = AddrModeFlat;
            sf.AddrFrame.Offset = ctx.Rbp;
            sf.AddrFrame.Mode   = AddrModeFlat;
            sf.AddrStack.Offset = ctx.Rsp;
            sf.AddrStack.Mode   = AddrModeFlat;

            char symBuf[sizeof(SYMBOL_INFO) + 256];
            auto* sym = reinterpret_cast<SYMBOL_INFO*>(symBuf);
            sym->SizeOfStruct = sizeof(SYMBOL_INFO);
            sym->MaxNameLen   = 255;

            IMAGEHLP_LINE64 line = {};
            line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

            fprintf(f, "[CRASH] Stack trace:\n");
            for (int i = 0; i < 32; ++i) {
                if (!StackWalk64(
                        IMAGE_FILE_MACHINE_AMD64,
                        GetCurrentProcess(),
                        GetCurrentThread(),
                        &sf, &ctx,
                        nullptr,
                        SymFunctionTableAccess64,
                        SymGetModuleBase64,
                        nullptr))
                    break;

                if (sf.AddrPC.Offset == 0)
                    break;

                DWORD64 symDisp = 0;
                if (SymFromAddr(GetCurrentProcess(),
                                sf.AddrPC.Offset, &symDisp, sym)) {
                    DWORD lineDisp = 0;
                    if (SymGetLineFromAddr64(GetCurrentProcess(),
                                            sf.AddrPC.Offset,
                                            &lineDisp, &line)) {
                        fprintf(f, "  [%2d] %s+0x%llX  (%s:%lu)\n",
                                i, sym->Name, (unsigned long long)symDisp,
                                line.FileName, line.LineNumber);
                    } else {
                        fprintf(f, "  [%2d] %s+0x%llX\n",
                                i, sym->Name, (unsigned long long)symDisp);
                    }
                } else {
                    fprintf(f, "  [%2d] 0x%016llX\n",
                            i, (unsigned long long)sf.AddrPC.Offset);
                }
            }

            fflush(f);
        }
        return EXCEPTION_EXECUTE_HANDLER;
    });

#ifdef FLUX_DEBUG
    AllocConsole();
    FILE* con;
    freopen_s(&con, "CONOUT$", "w", stdout);
#endif

    fluxLog("[MAIN] Step 1: started");

    FluxUI app(hInstance);


    app.build([&]() {

        auto result = createApp(&app);
        return result;
    });
 

    int w = FLUX_APP_WINDOW_WIDTH;
    int h = FLUX_APP_WINDOW_HEIGHT;
    bool fullscreen = static_cast<bool>(FLUX_APP_FULLSCREEN);
    bool maximize   = static_cast<bool>(FLUX_APP_MAXIMIZE);


    if (maximize && !fullscreen) {
        w = GetSystemMetrics(SM_CXSCREEN);
        h = GetSystemMetrics(SM_CYSCREEN);

    }

    app.createWindow(FLUX_APP_NAME, w, h);
    


    
    
    if (fullscreen) {
        // True borderless fullscreen: strip title bar/border, size to the
        // full monitor rect (not just the work area, which excludes the
        // taskbar). SW_MAXIMIZE alone can't do this — it keeps window chrome.
        HWND hwnd = app.getWindow();
        LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
        style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX |
                   WS_MAXIMIZEBOX | WS_SYSMENU);
        SetWindowLongPtr(hwnd, GWL_STYLE, style);

        HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(hmon, &mi);

        SetWindowPos(hwnd, HWND_TOP,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right  - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_NOZORDER | SWP_FRAMECHANGED);
    } else if (maximize) {
        ShowWindow(app.getWindow(), SW_MAXIMIZE);
    }
    int result = app.run();


    fclose(getDebugLogFile());
    return result;
}
#endif