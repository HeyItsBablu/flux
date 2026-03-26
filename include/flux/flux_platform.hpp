// flux/flux_platform.hpp
#pragma once

#ifdef _WIN32
    #include <windows.h>
    #include <windowsx.h>
    #include <gdiplus.h>
    #pragma comment(lib, "gdiplus.lib")

    using NativeWindow  = HWND;
    using NativeContext = HDC;
    using NativeFont    = HFONT;
    using NativeColor   = COLORREF;
    using AppInstance   = HINSTANCE;
    using TimerID = UINT;

    struct GraphicsContext {
        NativeContext hdc;
        explicit GraphicsContext(NativeContext h) : hdc(h) {}
    };

    struct BackBuffer {
        HDC     hdc    = nullptr;
        HBITMAP hbm    = nullptr;
        HBITMAP hbmOld = nullptr;
        int     width  = 0;
        int     height = 0;

        bool valid() const { return hdc != nullptr; }

        void create(HWND hwnd, int w, int h) {
            if (hdc && (w != width || h != height))
                destroy();
            if (!hdc) {
                HDC screen = GetDC(hwnd);
                hdc    = CreateCompatibleDC(screen);
                hbm    = CreateCompatibleBitmap(screen, w, h);
                hbmOld = (HBITMAP)SelectObject(hdc, hbm);
                ReleaseDC(hwnd, screen);
                width = w; height = h;
            }
        }

        void destroy() {
            if (hdc) {
                SelectObject(hdc, hbmOld);
                DeleteObject(hbm);
                DeleteDC(hdc);
                hdc = nullptr; hbm = nullptr; hbmOld = nullptr;
            }
        }

        GraphicsContext context() { return GraphicsContext(hdc); }
    };

#endif // _WIN32