#include "flux/flux.hpp"
#include "flux/flux_debug_log.hpp"
#ifdef _WIN32

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
    fluxLog("[MAIN] Step 2: FluxUI constructed");

    app.build([&]() {
        fluxLog("[MAIN] Step 2a: builder lambda called");
        auto result = createApp(&app);
        fluxLog("[MAIN] Step 2b: createApp returned result=" +
                std::string(result ? "valid" : "NULL"));
        return result;
    });
    fluxLog("[MAIN] Step 3: build returned");

    auto cfg = FluxAppWidget::getInstance();
    fluxLog("[MAIN] Step 4: cfg=" + std::string(cfg ? "valid" : "NULL"));
    if (!cfg) { fluxLog("[MAIN] FATAL: cfg null"); return -1; }

    int w = cfg->windowWidth;
    int h = cfg->windowHeight;
    fluxLog("[MAIN] Step 5: size=" + std::to_string(w) + "x" + std::to_string(h));

    if (cfg->fullscreen || cfg->maximize) {
        w = GetSystemMetrics(SM_CXSCREEN);
        h = GetSystemMetrics(SM_CYSCREEN);
        fluxLog("[MAIN] Step 5a: fullscreen " +
                std::to_string(w) + "x" + std::to_string(h));
    }

    fluxLog("[MAIN] Step 6: calling createWindow");
    app.createWindow(cfg->title, w, h);
    fluxLog("[MAIN] Step 7: createWindow returned");

    if (cfg->maximize || cfg->fullscreen)
        ShowWindow(app.getWindow(), SW_MAXIMIZE);

    fluxLog("[MAIN] Step 8: entering run");
    int result = app.run();
    fluxLog("[MAIN] Step 9: run returned " + std::to_string(result));

    fclose(getDebugLogFile());
    return result;
}
#endif