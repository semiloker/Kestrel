#ifndef INIT_D2D1_BI_H
#define INIT_D2D1_BI_H

#include "d2d1.h"

class init_d2d1_bi
{
public:
    ~init_d2d1_bi();

    void InitDirect2D();
    ID2D1HwndRenderTarget *GetOrCreateRenderTarget(HWND hwnd);
    void DiscardRenderTarget();
    void ResizeRenderTarget(HWND hwnd);

    void UpdateDpi(HWND hwnd);

private:
    ID2D1Factory *pD2DFactory = nullptr;
    ID2D1HwndRenderTarget *pRenderTarget = nullptr;

    float currentDpi = 96.0f;
};

#endif
