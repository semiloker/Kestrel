#ifndef DPI_BI_H
#define DPI_BI_H

#include <windows.h>

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

namespace dpi_bi
{
    UINT forWindow(HWND hwnd);

    float scaleForWindow(HWND hwnd);

    UINT forSystem();
}

#endif
