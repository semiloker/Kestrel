#include "overlay_bi.h"
#include "app_identity_bi.h"
#include "logger_bi.h"

#include <vector>
#include <cmath>

static const char OVERLAY_CLASS_NAME[] = APP_OVERLAY_CLASS;

static std::wstring toWide(const std::string &s)
{
    if (s.empty())
        return std::wstring();

    int len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), NULL, 0);
    if (len <= 0)
        return std::wstring();

    std::wstring out((size_t)len, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), &out[0], len);
    return out;
}

overlay_bi::overlay_bi() {}

overlay_bi::~overlay_bi()
{
    DestroyOverlayWindow();
}

LRESULT CALLBACK overlay_bi::StaticWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void overlay_bi::CreateOverlayWindow(HINSTANCE hInstance, HWND parentHwnd)
{
    (void)parentHwnd;

    if (g_hwnd != NULL && IsWindow(g_hwnd))
        return;

    hInst = hInstance;

    WNDCLASSA wc = {};
    wc.lpfnWndProc = StaticWindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = OVERLAY_CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;

    WNDCLASSA existing = {};
    if (!GetClassInfoA(hInstance, OVERLAY_CLASS_NAME, &existing))
        RegisterClassA(&wc);

    int w = (int)(layout.panelWidthFor(HUD_MIN_COLUMNS) + 0.5f);
    int h = (int)(layout.padTop + 6.0f * layout.lineHeight + layout.graphHeight * graphHeightMultiplier +
                  layout.padBottom + 0.5f);

    g_hwnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        OVERLAY_CLASS_NAME,
        APP_NAME " HUD",
        WS_POPUP,
        0, 0, w, h,
        NULL, NULL, hInstance, NULL);

    if (g_hwnd == NULL)
        return;

    BOOL disableTransitions = TRUE;
    DwmSetWindowAttribute(g_hwnd, DWMWA_TRANSITIONS_FORCEDISABLED,
                          &disableTransitions, sizeof(disableTransitions));

    BOOL disablePeek = TRUE;
    DwmSetWindowAttribute(g_hwnd, DWMWA_DISALLOW_PEEK, &disablePeek, sizeof(disablePeek));

    updateClickThrough();

    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);

    Render();
}

void overlay_bi::DestroyOverlayWindow()
{
    releaseGraphics();
    releaseSurface();

    if (g_hwnd && IsWindow(g_hwnd))
        DestroyWindow(g_hwnd);

    g_hwnd = NULL;
}

void overlay_bi::updateClickThrough()
{
    if (!g_hwnd || !IsWindow(g_hwnd))
        return;

    LONG_PTR ex = GetWindowLongPtr(g_hwnd, GWL_EXSTYLE);
    if (clickable)
        ex &= ~WS_EX_TRANSPARENT;
    else
        ex |= WS_EX_TRANSPARENT;
    SetWindowLongPtr(g_hwnd, GWL_EXSTYLE, ex);
}

void overlay_bi::ForceTopMost()
{
    if (!g_hwnd || !IsWindow(g_hwnd))
        return;

    if (GetWindow(g_hwnd, GW_HWNDPREV) == NULL)
        return;

    DWORD now = GetTickCount();
    if (lastTopMostTick != 0 && (now - lastTopMostTick) < 500)
        return;

    lastTopMostTick = now;

    SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW);
}

void overlay_bi::UpdatePosition()
{
    Render();
}

const D2D1_COLOR_F &overlay_bi::resolveColor(hud_color_bi c) const
{
    switch (c)
    {
    case HUD_COLOR_BLUE:
        return theme.blue;
    case HUD_COLOR_GREEN:
        return theme.green;
    case HUD_COLOR_ORANGE:
        return theme.orange;
    case HUD_COLOR_CYAN:
        return theme.cyan;
    case HUD_COLOR_MAGENTA:
        return theme.magenta;
    case HUD_COLOR_RED:
        return theme.red;
    default:
        return theme.white;
    }
}

void overlay_bi::setScale(int percent)
{
    if (percent < 50)
        percent = 50;
    if (percent > 250)
        percent = 250;

    hudScale = percent;

    float s = (float)percent / 100.0f;

    layout.padLeft = 9.0f * s;
    layout.padRight = 9.0f * s;
    layout.padBottom = 17.0f * s;
    layout.padTop = 16.0f * s;
    layout.lineHeight = 16.4f * s;
    layout.cornerRadius = 10.0f * s;
    layout.graphHeight = 50.6f * s;
    layout.graphGap = 13.0f * s;
    layout.charAdvance = (223.0f / (float)HUD_MIN_COLUMNS) * s;
    layout.graphTopNudge = -1.0f * s;
    layout.axisStroke = 1.5f * s;
    layout.graphStroke = 1.0f * s;
    layout.gridStroke = 1.0f * s;

    if (pBrush)
    {
        pBrush->Release();
        pBrush = nullptr;
    }
    if (pTextFormat)
    {
        pTextFormat->Release();
        pTextFormat = nullptr;
    }
    if (pRT)
    {
        pRT->Release();
        pRT = nullptr;
    }

    releaseSurface();

    traceRenders = 4;
    log_bi::write("overlay scale set to %d%%", hudScale);
}

bool overlay_bi::ensureGraphics()
{
    if (!pFactory)
    {
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &pFactory)) || !pFactory)
        {
            pFactory = nullptr;
            return false;
        }
    }

    if (!pDWrite)
    {
        HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                         __uuidof(IDWriteFactory),
                                         reinterpret_cast<IUnknown **>(&pDWrite));
        if (FAILED(hr) || !pDWrite)
        {
            pDWrite = nullptr;
            return false;
        }
    }

    if (!setupFont())
        return false;

    if (!pRT)
    {
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f, 96.0f,
            D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE,
            D2D1_FEATURE_LEVEL_DEFAULT);

        if (FAILED(pFactory->CreateDCRenderTarget(&props, &pRT)) || !pRT)
        {
            pRT = nullptr;
            return false;
        }

        pRT->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        pRT->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    }

    return true;
}

bool overlay_bi::setupFont()
{
    if (pTextFormat)
        return true;

    if (!pDWrite)
        return false;

    IDWriteFontCollection *collection = nullptr;
    if (FAILED(pDWrite->GetSystemFontCollection(&collection, FALSE)) || !collection)
        return false;

    const wchar_t *candidates[] = {L"Cascadia Mono", L"Consolas", L"Courier New"};

    IDWriteFont *font = nullptr;
    const wchar_t *chosenFamily = nullptr;

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]) && !font; ++i)
    {
        UINT32 index = 0;
        BOOL exists = FALSE;

        if (FAILED(collection->FindFamilyName(candidates[i], &index, &exists)) || !exists)
            continue;

        IDWriteFontFamily *family = nullptr;
        if (FAILED(collection->GetFontFamily(index, &family)) || !family)
            continue;

        if (SUCCEEDED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL,
                                                   DWRITE_FONT_STRETCH_NORMAL,
                                                   DWRITE_FONT_STYLE_NORMAL,
                                                   &font)) &&
            font)
        {
            chosenFamily = candidates[i];
        }

        family->Release();
    }

    if (!font || !chosenFamily)
    {
        collection->Release();
        return false;
    }

    float ascentPx = 0.0f;

    IDWriteFontFace *face = nullptr;
    if (SUCCEEDED(font->CreateFontFace(&face)) && face)
    {
        DWRITE_FONT_METRICS fm;
        face->GetMetrics(&fm);

        UINT32 codepoint = (UINT32)L'0';
        UINT16 glyph = 0;
        DWRITE_GLYPH_METRICS gm;

        if (fm.designUnitsPerEm > 0 &&
            SUCCEEDED(face->GetGlyphIndices(&codepoint, 1, &glyph)) &&
            SUCCEEDED(face->GetDesignGlyphMetrics(&glyph, 1, &gm, FALSE)) &&
            gm.advanceWidth > 0)
        {
            float upem = (float)fm.designUnitsPerEm;
            fontSize = layout.charAdvance * upem / (float)gm.advanceWidth;

            glyphTopOffset = ((float)fm.ascent - (float)fm.capHeight) * fontSize / upem;
            ascentPx = (float)fm.ascent * fontSize / upem;
        }

        face->Release();
    }

    HRESULT hr = pDWrite->CreateTextFormat(
        chosenFamily, NULL,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        fontSize, L"en-us", &pTextFormat);

    font->Release();
    collection->Release();

    if (FAILED(hr) || !pTextFormat)
    {
        pTextFormat = nullptr;
        return false;
    }

    pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    pTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    pTextFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    if (ascentPx > 0.0f)
        pTextFormat->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM,
                                    layout.lineHeight, ascentPx);

    return true;
}

void overlay_bi::releaseGraphics()
{
    if (pBrush)
    {
        pBrush->Release();
        pBrush = nullptr;
    }
    if (pTextFormat)
    {
        pTextFormat->Release();
        pTextFormat = nullptr;
    }
    if (pRT)
    {
        pRT->Release();
        pRT = nullptr;
    }
    if (pDWrite)
    {
        pDWrite->Release();
        pDWrite = nullptr;
    }
    if (pFactory)
    {
        pFactory->Release();
        pFactory = nullptr;
    }
}

bool overlay_bi::ensureSurface(int width, int height)
{
    if (width <= 0 || height <= 0)
        return false;

    panelW = width;
    panelH = height;

    int allocW = width + 32;
    int allocH = height + 64;

    if (memDC && dib && surfaceW >= width && surfaceH >= height &&
        surfaceW <= allocW * 2 && surfaceH <= allocH * 2)
    {
        return true;
    }

    releaseSurface();

    HDC screenDC = GetDC(NULL);
    if (!screenDC)
        return false;

    memDC = CreateCompatibleDC(screenDC);
    ReleaseDC(NULL, screenDC);

    if (!memDC)
        return false;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = allocW;
    bmi.bmiHeader.biHeight = -allocH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    dib = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &dibBits, NULL, 0);
    if (!dib)
    {
        DeleteDC(memDC);
        memDC = NULL;
        return false;
    }

    oldBitmap = (HBITMAP)SelectObject(memDC, dib);
    surfaceW = allocW;
    surfaceH = allocH;

    panelW = width;
    panelH = height;

    return true;
}

void overlay_bi::releaseSurface()
{
    if (memDC)
    {
        if (oldBitmap)
        {
            SelectObject(memDC, oldBitmap);
            oldBitmap = NULL;
        }
        DeleteDC(memDC);
        memDC = NULL;
    }

    if (dib)
    {
        DeleteObject(dib);
        dib = NULL;
    }

    dibBits = nullptr;
    surfaceW = 0;
    surfaceH = 0;
    panelW = 0;
    panelH = 0;
}

void overlay_bi::drawLine(const std::string &text, float startColumn, float y, const D2D1_COLOR_F &color)
{
    if (text.empty() || !pTextFormat || !pBrush)
        return;

    float advance = layout.charAdvance;

    size_t i = 0;
    while (i < text.size())
    {
        bool isBracket = (text[i] == '[' || text[i] == ']');

        size_t j = i;
        while (j < text.size() && ((text[j] == '[' || text[j] == ']') == isBracket))
            ++j;

        std::wstring run = toWide(text.substr(i, j - i));
        float x = layout.padLeft + (startColumn + (float)i) * advance;

        pBrush->SetColor(isBracket ? theme.bracket : color);

        D2D1_RECT_F rect = D2D1::RectF(x, y, curPanelW, y + layout.lineHeight);
        pRT->DrawText(run.c_str(), (UINT32)run.size(), pTextFormat, rect, pBrush,
                      D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);

        i = j;
    }
}

void overlay_bi::drawSeries(const hud_series_bi &series, double scaleMax,
                            float top, float axisY, const D2D1_COLOR_F &color)
{
    size_t n = series.size();
    if (n < 2 || !pFactory || !pBrush)
        return;

    float left = layout.padLeft;
    float right = curPanelW - layout.padRight;
    float height = axisY - top;

    float step = (right - left) / (float)(HUD_SAMPLE_COUNT - 1);

    ID2D1PathGeometry *geometry = nullptr;
    if (FAILED(pFactory->CreatePathGeometry(&geometry)) || !geometry)
        return;

    ID2D1GeometrySink *sink = nullptr;
    if (FAILED(geometry->Open(&sink)) || !sink)
    {
        geometry->Release();
        return;
    }

    for (size_t i = 0; i < n; ++i)
    {
        double v = series.at(i);
        if (v < 0.0)
            v = 0.0;

        float x = right - (float)(n - 1 - i) * step;
        float y = axisY - (float)(v / scaleMax) * height;

        D2D1_POINT_2F pt = D2D1::Point2F(x, y);

        if (i == 0)
            sink->BeginFigure(pt, D2D1_FIGURE_BEGIN_HOLLOW);
        else
            sink->AddLine(pt);
    }

    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    sink->Release();

    pBrush->SetColor(color);
    pRT->DrawGeometry(geometry, pBrush, layout.graphStroke);

    geometry->Release();
}

float overlay_bi::measureLayout(const std::vector<hud_element_bi> &elements) const
{
    float y = layout.padTop;
    bool lastWasGraph = false;

    for (size_t i = 0; i < elements.size(); ++i)
    {
        if (elements[i].kind == HUD_EL_ROW)
        {
            y += layout.lineHeight;
            lastWasGraph = false;
        }
        else
        {
            y += layout.graphTopNudge + layout.graphHeight * graphHeightMultiplier + layout.graphGap;
            lastWasGraph = true;
        }
    }

    if (lastWasGraph)
        y -= layout.graphGap;

    return y;
}

void overlay_bi::drawOneGraph(hud_graph_id_bi group, float top, float axisY)
{
    float left = layout.padLeft;
    float right = curPanelW - layout.padRight;

    int divisions = layout.gridDivisions > 0 ? layout.gridDivisions : 1;
    float span = (right - left) - 1.0f;
    float stepX = span / (float)divisions;

    for (int i = 0; i <= divisions; ++i)
    {
        bool isEdge = (i == 0 || i == divisions);

        float x = left + 0.5f + (float)i * stepX;

        pBrush->SetColor(isEdge ? theme.axis : theme.grid);
        pRT->DrawLine(D2D1::Point2F(x, top),
                      D2D1::Point2F(x, axisY),
                      pBrush, layout.gridStroke);
    }

    pBrush->SetColor(theme.axis);
    pRT->DrawLine(D2D1::Point2F(left, axisY),
                  D2D1::Point2F(right, axisY),
                  pBrush, layout.axisStroke);

    double scaleMax = 0.0;

    if (group == HUD_G_PERCENT || group == HUD_G_MEMORY)
    {
        scaleMax = 100.0;
    }
    else
    {
        for (int i = 0; i < HUD_M_COUNT; ++i)
        {
            const hud_metric_bi &m = hud.metrics[i];
            if (m.graph == group && m.show && m.graphed && m.available &&
                m.series.maximum() > scaleMax)
            {
                scaleMax = m.series.maximum();
            }
        }

        double headroom = scaleMax * 1.2;
        if (headroom < 1.0)
            headroom = 1.0;

        double step = pow(10.0, floor(log10(headroom)));
        scaleMax = ceil(headroom / step) * step;
    }

    if (scaleMax < 1.0)
        scaleMax = 1.0;

    for (int i = HUD_M_COUNT - 1; i >= 0; --i)
    {
        const hud_metric_bi &m = hud.metrics[i];
        if (m.graph == group && m.show && m.graphed && m.available)
            drawSeries(m.series, scaleMax, top, axisY, resolveColor(m.color));
    }
}

void overlay_bi::Render()
{
    bool trace = traceRenders > 0;
    if (trace)
        --traceRenders;

    if (!g_hwnd || !IsWindow(g_hwnd))
    {
        if (trace)
            log_bi::write("overlay trace: no window");
        return;
    }

    if (!ensureGraphics())
    {
        if (trace)
            log_bi::write("overlay trace: ensureGraphics failed");
        return;
    }

    hud.buildLayoutInto(layoutScratch);

    const std::vector<hud_element_bi> &elements = layoutScratch;
    if (elements.empty())
    {
        if (trace)
            log_bi::write("overlay trace: layout empty");
        return;
    }

    int columns = hud.columnsFor(elements);

    curPanelW = layout.panelWidthFor(columns);
    curPanelH = measureLayout(elements) + layout.padBottom;

    int w = (int)(curPanelW + 0.5f);
    int h = (int)(curPanelH + 0.5f);

    if (trace)
        log_bi::write("overlay trace: scale=%d cols=%d panel=%dx%d font=%.2f", hudScale, columns,
                      w, h, fontSize);

    if (!ensureSurface(w, h))
    {
        if (trace)
            log_bi::write("overlay trace: ensureSurface(%d,%d) failed", w, h);
        return;
    }

    RECT rc = {0, 0, panelW, panelH};
    HRESULT bindHr = pRT->BindDC(memDC, &rc);
    if (FAILED(bindHr))
    {
        if (trace)
            log_bi::write("overlay trace: BindDC failed 0x%08lX", (unsigned long)bindHr);
        return;
    }

    if (!pBrush)
    {
        if (FAILED(pRT->CreateSolidColorBrush(theme.white, &pBrush)) || !pBrush)
        {
            pBrush = nullptr;
            if (trace)
                log_bi::write("overlay trace: CreateSolidColorBrush failed");
            return;
        }
    }

    pRT->BeginDraw();
    pRT->SetTransform(D2D1::Matrix3x2F::Identity());
    pRT->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    pBrush->SetColor(theme.panel);

    D2D1_ROUNDED_RECT panelRect = {
        D2D1::RectF(0.0f, 0.0f, (float)panelW, (float)panelH),
        layout.cornerRadius,
        layout.cornerRadius};

    pRT->FillRoundedRectangle(&panelRect, pBrush);

    float y = layout.padTop;

    for (size_t i = 0; i < elements.size(); ++i)
    {
        const hud_element_bi &el = elements[i];

        if (el.kind == HUD_EL_ROW)
        {
            const D2D1_COLOR_F &rowColor = resolveColor(el.row.color);
            float textY = y - glyphTopOffset;

            drawLine(el.row.left, 0.0f, textY, rowColor);

            if (!el.row.right.empty())
            {
                float column = (float)columns - (float)el.row.right.size();
                if (column < 0.0f)
                    column = 0.0f;

                drawLine(el.row.right, column, textY, rowColor);
            }

            y += layout.lineHeight;
        }
        else
        {
            float top = y + layout.graphTopNudge;
            float axisY = top + layout.graphHeight * graphHeightMultiplier;

            drawOneGraph(el.graph, top, axisY);

            y = axisY + layout.graphGap;
        }
    }

    HRESULT endHr = pRT->EndDraw();
    if (FAILED(endHr))
    {
        if (trace)
            log_bi::write("overlay trace: EndDraw failed 0x%08lX", (unsigned long)endHr);
        releaseGraphics();
        return;
    }

    GdiFlush();

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    POINT ptDst;
    ptDst.x = (corner == CORNER_TOP_RIGHT || corner == CORNER_BOTTOM_RIGHT)
                  ? screenW - panelW - margin
                  : margin;
    ptDst.y = (corner == CORNER_BOTTOM_LEFT || corner == CORNER_BOTTOM_RIGHT)
                  ? screenH - panelH - margin
                  : margin;

    POINT ptSrc = {0, 0};
    SIZE size = {panelW, panelH};

    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = (BYTE)overlayAlpha;
    blend.AlphaFormat = AC_SRC_ALPHA;

    HDC screenDC = GetDC(NULL);
    if (screenDC)
    {
        BOOL ulwOk = UpdateLayeredWindow(g_hwnd, screenDC, &ptDst, &size,
                                         memDC, &ptSrc, 0, &blend, ULW_ALPHA);
        if (trace)
            log_bi::write("overlay trace: ULW pos=%d,%d size=%dx%d ok=%d err=%lu",
                          (int)ptDst.x, (int)ptDst.y, (int)size.cx, (int)size.cy,
                          (int)ulwOk, (unsigned long)(ulwOk ? 0 : GetLastError()));
        ReleaseDC(NULL, screenDC);
    }
    else if (trace)
    {
        log_bi::write("overlay trace: GetDC(NULL) failed");
    }

    ForceTopMost();
}
