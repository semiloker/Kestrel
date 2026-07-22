#ifndef DRAW_BATTERYINFO_H
#define DRAW_BATTERYINFO_H

#include <d2d1.h>

#include "BatteryInfo.h"
#include "init_dwrite_bi.h"
#include "overlay_bi.h"
#include "resource_usage_bi.h"
#include <vector>
#include <map>
#include <cmath>

class draw_batteryinfo_bi
{
public:
    draw_batteryinfo_bi()
    {
        textColor = D2D1::ColorF(0.1f, 0.1f, 0.1f);
        labelColor = D2D1::ColorF(0.4f, 0.4f, 0.4f);
        separatorColor = D2D1::ColorF(0.8f, 0.8f, 0.8f);
        backgroundColor = D2D1::ColorF(0.98f, 0.98f, 0.98f);
        headerColor = D2D1::ColorF(0.2f, 0.4f, 0.8f);

        accentColor = D2D1::ColorF(0.2f, 0.4f, 0.8f);
        nightMode = false;

        pLabelBrush = nullptr;
        pValueBrush = nullptr;
        pHeaderBrush = nullptr;
        pSeparatorBrush = nullptr;

        pSwitchOnBrush = nullptr;
        pSwitchOffBrush = nullptr;
        pSwitchKnobBrush = nullptr;

        pScrollBarBrush = nullptr;

        pBackgroundBrush = nullptr;

        pAccentBrush = nullptr;

        colorPalette =
        {
            D2D1::ColorF(0.20f, 0.40f, 0.80f),
            D2D1::ColorF(0.80f, 0.20f, 0.20f),
            D2D1::ColorF(0.20f, 0.70f, 0.30f),
            D2D1::ColorF(0.85f, 0.55f, 0.10f),
            D2D1::ColorF(0.55f, 0.15f, 0.85f),
            D2D1::ColorF(0.10f, 0.75f, 0.85f),
            D2D1::ColorF(0.95f, 0.20f, 0.65f),
            D2D1::ColorF(0.95f, 0.75f, 0.05f),
            D2D1::ColorF(0.50f, 0.50f, 0.50f),
            D2D1::ColorF(0.15f, 0.35f, 0.15f),
            D2D1::ColorF(0.40f, 0.20f, 0.10f),
            D2D1::ColorF(0.12f, 0.12f, 0.45f),
            D2D1::ColorF(0.30f, 0.60f, 0.60f),
            D2D1::ColorF(0.85f, 0.40f, 0.60f),
            D2D1::ColorF(0.65f, 0.40f, 0.95f),
            D2D1::ColorF(0.15f, 0.85f, 0.45f)};
        }

    ~draw_batteryinfo_bi()
    {
        auto SafeRelease = [](ID2D1SolidColorBrush *&b)
        { if (b) { b->Release(); b = nullptr; } };

        SafeRelease(pLabelBrush);
        SafeRelease(pValueBrush);
        SafeRelease(pHeaderBrush);
        SafeRelease(pSeparatorBrush);

        SafeRelease(pSwitchOnBrush);
        SafeRelease(pSwitchOffBrush);
        SafeRelease(pSwitchKnobBrush);

        SafeRelease(pScrollBarBrush);
        SafeRelease(pBackgroundBrush);

        SafeRelease(pAccentBrush);
    }

    enum selected_option
    {
        BATTERY_INFO,
        SETTINGS,
        ABOUT_ME,
        APPEARANCE,
    };

    selected_option selectedTab = BATTERY_INFO;

    void setTab(selected_option tab);

    void clampScroll();

    void drawHeaderBatteryInfoD2D(ID2D1HwndRenderTarget *pRT, batteryinfo_bi *bi_bi, init_dwrite_bi *initdwrite_bi, int startX, int startY, int lineHeight);
    void drawHeaderSettingsD2D(ID2D1HwndRenderTarget *pRT, init_dwrite_bi *initdwrite_bi, overlay_bi *ov_bi, resource_usage_bi *ru_bi, batteryinfo_bi *bi_bi);
    void drawHeaderAboutMeD2D(ID2D1HwndRenderTarget *pRT, init_dwrite_bi *initdwrite_bi, overlay_bi *ov_bi, resource_usage_bi *ru_bi, batteryinfo_bi *bi_bi);
    void drawHeaderAppearanceD2D(ID2D1HwndRenderTarget *pRT, init_dwrite_bi *initdwrite_bi, overlay_bi *ov_bi);
    void drawHeaders(ID2D1HwndRenderTarget *pRT, init_dwrite_bi *initdwrite_bi, int startX = 20, int startY = 20, int lineHeight = 24);

    bool initBrush(ID2D1HwndRenderTarget *pRT);
    bool updateBrushes(ID2D1HwndRenderTarget *pRT);
    bool clearBackground(ID2D1HwndRenderTarget *pRT);

    bool isCursorInBatteryStatus(POINT cursorPos);
    bool isCursorInSettings(POINT cursorPos);
    bool isCursorInAboutMe(POINT cursorPos);
    bool isCursorInAppearance(POINT cursorPos);

    void drawToggleSwitch(ID2D1HwndRenderTarget *pRT, init_dwrite_bi *initdwrite_bi,
                          float x, float y, bool &toggleState, const std::wstring &labelText);
    bool handleSwitchClick(POINT cursorPos);

    bool handleColorClick(POINT cursorPos);
    bool handleCornerClick(POINT cursorPos, overlay_bi *ov_bi);
    bool handleAppearanceClick(POINT cursorPos, overlay_bi *ov_bi);

    const D2D1_COLOR_F &getAccentColor() const { return accentColor; }
    void setAccentColor(const D2D1_COLOR_F &c) { accentColor = c; }

    bool getNightMode() const { return nightMode; }
    void setNightMode(bool on) { nightMode = on; }

    float scrollOffsetY = 0.0f;
    float contentHeight = 0.0f;
    float viewHeight = 0.0f;

    float tabScroll[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    D2D1_RECT_F rectBatteryStatus{};
    D2D1_RECT_F rectSettings{};
    D2D1_RECT_F rectAboutMe{};
    D2D1_RECT_F rectAppearance{};

private:
    D2D1_COLOR_F textColor;
    D2D1_COLOR_F labelColor;
    D2D1_COLOR_F separatorColor;
    D2D1_COLOR_F backgroundColor;
    D2D1_COLOR_F headerColor;

    D2D1_COLOR_F accentColor;
    std::vector<D2D1_COLOR_F> colorPalette;
    bool nightMode;

    ID2D1SolidColorBrush *pLabelBrush;
    ID2D1SolidColorBrush *pValueBrush;
    ID2D1SolidColorBrush *pHeaderBrush;

    ID2D1SolidColorBrush *pSeparatorBrush;

    ID2D1SolidColorBrush *pSwitchOnBrush;
    ID2D1SolidColorBrush *pSwitchOffBrush;
    ID2D1SolidColorBrush *pSwitchKnobBrush;

    ID2D1SolidColorBrush *pScrollBarBrush;

    ID2D1SolidColorBrush *pBackgroundBrush;

    ID2D1SolidColorBrush *pAccentBrush;

    struct SwitchRect
    {
        D2D1_RECT_F rect;
        bool *pState;
    };

    std::vector<SwitchRect> switchRects;
    std::vector<D2D1_RECT_F> colorRects;
    std::vector<D2D1_RECT_F> cornerRects;

    FLOAT maxWidth = 0;
};

#endif
