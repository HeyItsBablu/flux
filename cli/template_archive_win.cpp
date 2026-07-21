#include "flux_template_resource.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace
{
    const unsigned char *g_data = nullptr;
    size_t g_size = 0;
    bool g_loaded = false;

    void ensure_loaded()
    {
        if (g_loaded)
            return;
        g_loaded = true;

        HMODULE self = GetModuleHandle(nullptr);
        HRSRC res = FindResource(self, MAKEINTRESOURCE(IDR_FLUX_TEMPLATE), RT_RCDATA);
        if (!res)
            return;
        HGLOBAL h = LoadResource(self, res);
        if (!h)
            return;
        g_data = static_cast<const unsigned char *>(LockResource(h));
        g_size = SizeofResource(self, res);
    }
}

extern const unsigned char *flux_template_data()
{
    ensure_loaded();
    return g_data;
}

extern size_t flux_template_size()
{
    ensure_loaded();
    return g_size;
}