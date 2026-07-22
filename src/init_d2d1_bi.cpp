#include "init_d2d1_bi.h"
#include "dpi_bi.h"

void init_d2d1_bi::InitDirect2D()
{
    if (!pD2DFactory)
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &pD2DFactory);
}

ID2D1HwndRenderTarget *init_d2d1_bi::GetOrCreateRenderTarget(HWND hwnd)
{
    if (!pRenderTarget)
    {
        RECT rc;
        GetClientRect(hwnd, &rc);

        float dpi = (float)dpi_bi::forWindow(hwnd);

        D2D1_RENDER_TARGET_PROPERTIES rtProps =
            D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_UNKNOWN),
                dpi, dpi);

        D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps =
            D2D1::HwndRenderTargetProperties(hwnd,
                                             D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top));

        HRESULT hr = pD2DFactory->CreateHwndRenderTarget(rtProps, hwndProps, &pRenderTarget);
        if (FAILED(hr))
        {
            pRenderTarget = nullptr;
        }
        else
        {
            currentDpi = dpi;
        }
    }
    return pRenderTarget;
}

void init_d2d1_bi::ResizeRenderTarget(HWND hwnd)
{
    if (pRenderTarget)
    {
        RECT rc;
        GetClientRect(hwnd, &rc);
        D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);
        pRenderTarget->Resize(size);
    }
}

void init_d2d1_bi::UpdateDpi(HWND hwnd)
{
    if (!pRenderTarget)
        return;

    float dpi = (float)dpi_bi::forWindow(hwnd);

    if (dpi > currentDpi - 0.5f && dpi < currentDpi + 0.5f)
        return;

    pRenderTarget->SetDpi(dpi, dpi);
    currentDpi = dpi;

    ResizeRenderTarget(hwnd);
}

void init_d2d1_bi::DiscardRenderTarget()
{
    if (pRenderTarget)
    {
        pRenderTarget->Release();
        pRenderTarget = nullptr;
    }
}

init_d2d1_bi::~init_d2d1_bi()
{
    DiscardRenderTarget();

    if (pD2DFactory)
    {
        pD2DFactory->Release();
        pD2DFactory = nullptr;
    }
}
