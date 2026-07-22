#include "dpi_bi.h"

namespace
{
    typedef UINT(WINAPI *GetDpiForWindow_t)(HWND);
    typedef UINT(WINAPI *GetDpiForSystem_t)(void);

    GetDpiForWindow_t g_getDpiForWindow = NULL;
    GetDpiForSystem_t g_getDpiForSystem = NULL;
    bool g_resolved = false;

    void resolve()
    {
        if (g_resolved)
            return;

        g_resolved = true;

        HMODULE user32 = GetModuleHandleA("user32.dll");
        if (!user32)
            return;

        g_getDpiForWindow = (GetDpiForWindow_t)(void *)GetProcAddress(user32, "GetDpiForWindow");
        g_getDpiForSystem = (GetDpiForSystem_t)(void *)GetProcAddress(user32, "GetDpiForSystem");
    }

    UINT legacyDpi()
    {
        HDC screen = GetDC(NULL);
        if (!screen)
            return 96;

        int dpi = GetDeviceCaps(screen, LOGPIXELSX);
        ReleaseDC(NULL, screen);

        return dpi > 0 ? (UINT)dpi : 96;
    }
}

UINT dpi_bi::forWindow(HWND hwnd)
{
    resolve();

    if (hwnd && g_getDpiForWindow)
    {
        UINT dpi = g_getDpiForWindow(hwnd);
        if (dpi > 0)
            return dpi;
    }

    return forSystem();
}

float dpi_bi::scaleForWindow(HWND hwnd)
{
    return (float)forWindow(hwnd) / 96.0f;
}

UINT dpi_bi::forSystem()
{
    resolve();

    if (g_getDpiForSystem)
    {
        UINT dpi = g_getDpiForSystem();
        if (dpi > 0)
            return dpi;
    }

    return legacyDpi();
}
