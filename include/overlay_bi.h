#ifndef OVERLAY_BI_H
#define OVERLAY_BI_H

#include <windows.h>
#include <dwmapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>

#include "hud_bi.h"

struct hud_layout_bi
{
    float padLeft = 9.0f;
    float padRight = 9.0f;
    float padBottom = 17.0f;

    float padTop = 16.0f;

    float lineHeight = 16.4f;

    float cornerRadius = 10.0f;

    float graphHeight = 50.6f;
    float graphGap = 13.0f;
    float graphStroke = 1.0f;

    float axisStroke = 1.5f;
    float gridStroke = 1.0f;

    int gridDivisions = 10;

    float charAdvance = 223.0f / (float)HUD_MIN_COLUMNS;

    float panelWidthFor(int columns) const
    {
        int c = columns < HUD_MIN_COLUMNS ? HUD_MIN_COLUMNS : columns;
        return padLeft + (float)c * charAdvance + padRight;
    }

    float contentWidthFor(int columns) const { return panelWidthFor(columns) - padLeft - padRight; }

    float graphTopNudge = -1.0f;
};

struct hud_theme_bi
{
    D2D1_COLOR_F panel = {0.00f, 0.00f, 0.00f, 0.85f};
    D2D1_COLOR_F white = {1.00f, 1.00f, 1.00f, 1.00f};
    D2D1_COLOR_F blue = {0.51f, 0.63f, 0.76f, 1.00f};
    D2D1_COLOR_F green = {0.64f, 0.75f, 0.55f, 1.00f};
    D2D1_COLOR_F orange = {0.85f, 0.71f, 0.52f, 1.00f};
    D2D1_COLOR_F cyan = {0.55f, 0.73f, 0.74f, 1.00f};
    D2D1_COLOR_F magenta = {0.75f, 0.60f, 0.75f, 1.00f};

    D2D1_COLOR_F red = {0.79f, 0.55f, 0.55f, 1.00f};

    D2D1_COLOR_F bracket = {0.55f, 0.55f, 0.55f, 1.00f};

    D2D1_COLOR_F axis = {0.55f, 0.55f, 0.55f, 1.00f};
    D2D1_COLOR_F grid = {0.34f, 0.34f, 0.34f, 1.00f};
};

class overlay_bi
{
public:
    enum corner_bi
    {
        CORNER_TOP_LEFT = 0,
        CORNER_TOP_RIGHT,
        CORNER_BOTTOM_LEFT,
        CORNER_BOTTOM_RIGHT
    };

    overlay_bi();
    ~overlay_bi();

    HWND g_hwnd = NULL;

    bool show_on_screen_display = false;
    bool autoHideOverlay = false;
    bool overlayAutoHidden = false;

    corner_bi corner = CORNER_TOP_RIGHT;
    int margin = 20;
    int refreshMs = HUD_SAMPLE_INTERVAL_MS;
    int overlayAlpha = 255;

    void setScale(int percent);
    int getScale() const { return hudScale; }

    hud_bi hud;
    hud_layout_bi layout;
    hud_theme_bi theme;

    void CreateOverlayWindow(HINSTANCE hInstance, HWND parentHwnd = NULL);
    void DestroyOverlayWindow();

    void Render();

    void UpdatePosition();
    void ForceTopMost();

    const D2D1_COLOR_F &resolveColor(hud_color_bi c) const;

    static LRESULT CALLBACK StaticWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

private:
    bool ensureGraphics();
    void releaseGraphics();
    bool ensureSurface(int width, int height);
    void releaseSurface();
    bool setupFont();

    void drawLine(const std::string &text, float startColumn, float y, const D2D1_COLOR_F &color);
    float measureLayout(const std::vector<hud_element_bi> &elements) const;
    void drawOneGraph(hud_graph_id_bi group, float top, float axisY);
    void drawSeries(const hud_series_bi &series, double scaleMax,
                    float top, float axisY, const D2D1_COLOR_F &color);

    ID2D1Factory *pFactory = nullptr;
    ID2D1DCRenderTarget *pRT = nullptr;
    IDWriteFactory *pDWrite = nullptr;
    IDWriteTextFormat *pTextFormat = nullptr;
    ID2D1SolidColorBrush *pBrush = nullptr;

    int hudScale = 100;
    int traceRenders = 0;

    float fontSize = 13.8f;
    float glyphTopOffset = 0.0f;

    float curPanelW = 241.0f;
    float curPanelH = 181.0f;

    HDC memDC = NULL;
    HBITMAP dib = NULL;
    HBITMAP oldBitmap = NULL;
    void *dibBits = nullptr;
    std::vector<hud_element_bi> layoutScratch;

    int surfaceW = 0;
    int surfaceH = 0;
    int panelW = 0;
    int panelH = 0;

    DWORD lastTopMostTick = 0;

    HINSTANCE hInst = NULL;
};

#endif
