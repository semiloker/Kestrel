#ifndef DRAW_BATTERYINFO_H
#define DRAW_BATTERYINFO_H

#include <d2d1.h>
#include <string>
#include <vector>
#include <tuple>
#include <unordered_map>

#include "BatteryInfo.h"
#include "capture_bi.h"
#include "init_dwrite_bi.h"
#include "overlay_bi.h"
#include "resource_usage_bi.h"

class draw_batteryinfo_bi
{
public:
    draw_batteryinfo_bi();
    ~draw_batteryinfo_bi();

    draw_batteryinfo_bi(const draw_batteryinfo_bi &) = delete;
    draw_batteryinfo_bi &operator=(const draw_batteryinfo_bi &) = delete;

    enum selected_option
    {
        BATTERY_INFO = 0,
        SETTINGS,
        CAPTURE,
        APPEARANCE,
        ABOUT_ME,
        TAB_COUNT
    };

    enum hit_kind_bi
    {
        HIT_NONE = 0,
        HIT_TAB,
        HIT_TOGGLE,
        HIT_ROW_CHIP,
        HIT_GRAPH_CHIP,
        HIT_RAIL,
        HIT_PRESET,
        HIT_THEME,
        HIT_ACCENT,
        HIT_CORNER,
        HIT_MARGIN,
        HIT_REFRESH,
        HIT_SCALE,
        HIT_MEMUNIT,
        HIT_ALPHA,
        HIT_AUTOHIDE,
        HIT_CAPTURE_RUN,
        HIT_USERPRESET_APPLY,
        HIT_USERPRESET_SAVE,
        HIT_ACTION,
        HIT_SEARCH,
        HIT_MOVEUP,
        HIT_MOVEDOWN,
        HIT_GRAPHHEIGHT,
        HIT_RESETORDER
    };

    enum action_bi
    {
        ACT_NONE = 0,
        ACT_RESTART_ADMIN,
        ACT_OPEN_REPO,
        ACT_OPEN_ISSUES,
        ACT_OPEN_LICENCE,
        ACT_OPEN_LOG,
        ACT_CHECK_UPDATE,
        ACT_DOWNLOAD_UPDATE,
        ACT_INSTALL_UPDATE,
        ACT_ROLLBACK,
        ACT_TOGGLE_CAPTURE,
        ACT_OPEN_CAPTURES,
        ACT_SAVE_PROFILE
    };

    struct capture_view_bi
    {
        bool recording = false;
        double seconds = 0.0;
        size_t frames = 0;
        double liveFps = 0.0;
        double liveLow1 = 0.0;
        bool liveLow1Valid = false;
        bool hasLast = false;
        capture_bi::summary_bi last;
        std::vector<capture_bi::summary_bi> history;
    };

    struct update_view_bi
    {
        int state = 0;
        std::string version;
        std::string message;
        int progress = 0;
        bool backupAvailable = false;
        std::string backupVersion;
    };

    enum settings_group_bi
    {
        GROUP_OVERLAY = 0,
        GROUP_FRAME,
        GROUP_GPU,
        GROUP_CPU,
        GROUP_BATTERY,
        GROUP_MEMORY,
        GROUP_BEHAVIOR,
        GROUP_COUNT
    };

    struct diag_bi
    {
        bool frameTiming = false;
        std::string frameReason;
        bool cpuPower = false;
        std::string gpuName;
        bool battery = false;
        std::string chemistry;
        int threads = 0;
    };

    struct click_result_bi
    {
        bool handled = false;
        bool needsSave = false;
        bool needsBrushRebuild = false;
        bool toggledAutostart = false;
        bool toggledAdmin = false;
        bool refreshChanged = false;
        int action = ACT_NONE;
    };

    struct hit_bi
    {
        D2D1_RECT_F rect;
        int kind;
        int index;
        bool *state;
    };

    void setTab(selected_option tab);
    void clampScroll();
    void scrollBy(float amount);

    bool initBrush(ID2D1HwndRenderTarget *pRT);
    bool updateBrushes(ID2D1HwndRenderTarget *pRT);
    bool clearBackground(ID2D1HwndRenderTarget *pRT);
    void releaseDeviceResources();

    void drawBatteryTab(ID2D1HwndRenderTarget *pRT, init_dwrite_bi *dw, batteryinfo_bi *bi);
    void drawSettingsTab(ID2D1HwndRenderTarget *pRT, init_dwrite_bi *dw,
                         overlay_bi *ov, resource_usage_bi *ru, batteryinfo_bi *bi);
    void drawCaptureTab(ID2D1HwndRenderTarget *pRT, init_dwrite_bi *dw,
                        const capture_view_bi &cap);
    float drawRunCard(init_dwrite_bi *dw, float L, float R, float y,
                      const capture_bi::summary_bi &s, const wchar_t *title);
    void drawAppearanceTab(ID2D1HwndRenderTarget *pRT, init_dwrite_bi *dw, overlay_bi *ov);
    void drawAboutTab(ID2D1HwndRenderTarget *pRT, init_dwrite_bi *dw, const diag_bi &diag,
                      const update_view_bi &upd);
    void drawTabBar(ID2D1HwndRenderTarget *pRT, init_dwrite_bi *dw);

    click_result_bi handleClick(POINT cursorPos, overlay_bi *ov,
                                resource_usage_bi *ru, batteryinfo_bi *bi);

    void applyPresetExternal(int preset, overlay_bi *ov, resource_usage_bi *ru,
                             batteryinfo_bi *bi)
    {
        applyPreset(preset, ov, ru, bi);
    }

    bool setHover(POINT cursorPos);
    bool clearHover();
    bool isOverInteractive(POINT cursorPos) const;

    bool beginScrollDrag(POINT cursorPos);
    void updateScrollDrag(POINT cursorPos);
    void endScrollDrag();
    bool isScrollDragging() const { return scrollDragging; }

    bool beginGraphHeightDrag(POINT cursorPos, overlay_bi *ov);
    void updateGraphHeightDrag(POINT cursorPos, overlay_bi *ov);
    void endGraphHeightDrag() { graphHeightDragging = false; }
    bool isGraphHeightDragging() const { return graphHeightDragging; }

    const D2D1_COLOR_F &getAccentColor() const { return accentColor; }
    void setAccentColor(const D2D1_COLOR_F &c) { accentColor = c; }

    bool getNightMode() const { return nightMode; }
    void setNightMode(bool on) { nightMode = on; }

    enum theme_bi {
        THEME_DARK = 0,
        THEME_LIGHT,
        THEME_NORD,
        THEME_DRACULA,
        THEME_GRUVBOX,
        THEME_DEUTERANOPIA_DARK,
        THEME_DEUTERANOPIA_LIGHT,
        THEME_PROTANOPIA_DARK,
        THEME_PROTANOPIA_LIGHT,
        THEME_TRITANOPIA_DARK,
        THEME_TRITANOPIA_LIGHT,
        THEME_COUNT
    };

    int getTheme() const { return themeIndex; }
    // resetAccent=false re-applies the theme's palette without overwriting the
    // user's chosen accent (used by brush rebuilds); true resets to the theme's
    // default accent (used when the user actively picks a theme).
    void setTheme(int index, bool resetAccent = true);
    static const char *themeName(int index);
    bool themeIsNight(int index) const;

    int getSettingsGroup() const { return settingsGroup; }
    void setSettingsGroup(int g);

    selected_option selectedTab = BATTERY_INFO;

    bool processElevated = true;

    int selectedRun = -1;

    std::string userPreset[3];

    std::string captureLayout(overlay_bi *ov, resource_usage_bi *ru, batteryinfo_bi *bi) const;
    void applyLayout(const std::string &s, overlay_bi *ov, resource_usage_bi *ru,
                     batteryinfo_bi *bi);

    float scrollOffsetY = 0.0f;
    float contentHeight = 0.0f;
    float viewHeight = 0.0f;

    float tabScroll[TAB_COUNT] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    std::string searchQuery;
    bool searchFocused = false;

    void clearLayoutCache();
    void setDWriteFactory(IDWriteFactory *factory) { dwriteFactory = factory; }

    struct palette_bi
    {
        D2D1_COLOR_F bg;
        D2D1_COLOR_F surface;
        D2D1_COLOR_F inset;
        D2D1_COLOR_F border;
        D2D1_COLOR_F borderStrong;
        D2D1_COLOR_F text;
        D2D1_COLOR_F muted;
        D2D1_COLOR_F faint;
        D2D1_COLOR_F disabled;
        D2D1_COLOR_F trackOff;
        D2D1_COLOR_F knob;
        D2D1_COLOR_F ok;
        D2D1_COLOR_F warn;
        D2D1_COLOR_F bad;
    };

private:
    struct row_bi
    {
        const wchar_t *label;
        bool *show;
        bool *graph;
        int color;
        bool available;
        const wchar_t *desc;
        int metricId = -1;
    };

    void fillR(float l, float t, float r, float b, const D2D1_COLOR_F &c);
    void fillRR(float l, float t, float r, float b, float radius, const D2D1_COLOR_F &c);
    void strokeRR(float l, float t, float r, float b, float radius,
                  const D2D1_COLOR_F &c, float width);
    void fillEl(float cx, float cy, float radius, const D2D1_COLOR_F &c);
    void line(float x1, float y1, float x2, float y2, const D2D1_COLOR_F &c, float width);
    void txt(IDWriteTextFormat *f, float l, float t, float r, float b,
             const D2D1_COLOR_F &c, const std::wstring &s,
             DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING,
             bool clip = false);

    void bar(float l, float t, float w, float h, float fraction, const D2D1_COLOR_F &c);
    void card(float l, float t, float r, float b);
    void eyebrow(init_dwrite_bi *dw, float x, float y, const std::wstring &s);
    void statCell(init_dwrite_bi *dw, float x, float y, float w,
                  const std::wstring &label, const std::wstring &value, bool missing);
    void toggle(float x, float y, bool on, bool dimmed);
    void chip(init_dwrite_bi *dw, float x, float y, float w,
              const std::wstring &label, bool on, bool dimmed);
    void button(init_dwrite_bi *dw, float x, float y, float w,
                const std::wstring &label, bool primary, bool hot);
    void logoMark(float x, float y, float size);
    void hoverRing(float l, float t, float r, float b, float radius);
    void hoverFill(float l, float t, float r, float b, float radius);

    void pushHit(const D2D1_RECT_F &rect, int kind, int index, bool *state);
    bool isHovered(int kind, int index) const;
    const hit_bi *findHit(POINT cursorPos) const;

    void beginFrame(ID2D1HwndRenderTarget *pRT);
    float contentLeft() const;
    float contentRight() const;

    D2D1_COLOR_F accentText() const;
    D2D1_COLOR_F tint(const D2D1_COLOR_F &c, float alpha) const;
    const D2D1_COLOR_F &metricColor(int index) const;

    int buildGroupRows(int group, overlay_bi *ov, resource_usage_bi *ru,
                       batteryinfo_bi *bi, std::vector<row_bi> &out) const;
    void applyPreset(int preset, overlay_bi *ov, resource_usage_bi *ru, batteryinfo_bi *bi);
    int detectPreset(overlay_bi *ov, resource_usage_bi *ru, batteryinfo_bi *bi);
    void snapshotRows(overlay_bi *ov, resource_usage_bi *ru, batteryinfo_bi *bi,
                      std::vector<bool> &out) const;
    void restoreRows(overlay_bi *ov, resource_usage_bi *ru, batteryinfo_bi *bi,
                     const std::vector<bool> &in) const;

    void drawOverlayPreview(init_dwrite_bi *dw, overlay_bi *ov, resource_usage_bi *ru,
                            float l, float t, float r, float b);
    D2D1_SIZE_F measureOverlayPanel(overlay_bi *ov) const;
    void drawPanelText(init_dwrite_bi *dw, const std::string &text, float startColumn,
                       float x, float y, float clipRight, const D2D1_COLOR_F &color,
                       const D2D1_COLOR_F &bracketColor, float advance);
    void drawPanelSeries(const hud_series_bi &series, double scaleMax,
                         float left, float right, float top, float axisY,
                         const D2D1_COLOR_F &color);
    void drawFooter(init_dwrite_bi *dw, const std::wstring &text);
    void drawScrollbar();

    D2D1_COLOR_F accentColor;
    std::vector<D2D1_COLOR_F> colorPalette;
    palette_bi pal;
    bool nightMode;
    int themeIndex = THEME_DARK;

    int settingsGroup = GROUP_OVERLAY;

    int cachedPreset = -1;
    bool settingsDirty = true;
    mutable std::vector<row_bi> rowScratch;
    mutable std::vector<hud_element_bi> panelScratch;
    float previewScale = 1.0f;

    ID2D1HwndRenderTarget *rt = nullptr;
    ID2D1SolidColorBrush *pBrush = nullptr;

    float viewWidth = 0.0f;

    int hoverKind = HIT_NONE;
    int hoverIndex = -1;

    D2D1_RECT_F scrollThumbRect;
    float scrollTrackTop = 0.0f;
    float scrollTrackHeight = 0.0f;
    float scrollThumbHeight = 0.0f;
    bool footerActive = false;
    bool scrollActive = false;
    bool scrollDragging = false;
    bool graphHeightDragging = false;
    D2D1_RECT_F graphSliderTrack = {0.0f, 0.0f, 0.0f, 0.0f};
    float dragStartY = 0.0f;
    float dragStartOffset = 0.0f;

    std::vector<hit_bi> hits;

    IDWriteFactory *dwriteFactory = nullptr;
    typedef std::tuple<const std::wstring, IDWriteTextFormat*, float, float, DWRITE_TEXT_ALIGNMENT> LayoutKey;
    struct LayoutHash {
        size_t operator()(const LayoutKey &k) const {
            size_t h = std::hash<std::wstring>()(std::get<0>(k));
            h ^= (size_t)std::get<1>(k) * 0x9e3779b9;
            h ^= (size_t)(std::get<2>(k) * 100.0f) * 0x9e3779b9;
            h ^= (size_t)(std::get<3>(k) * 100.0f) * 0x9e3779b9;
            h ^= std::get<4>(k) * 0x9e3779b9;
            return h;
        }
    };
    std::unordered_map<LayoutKey, IDWriteTextLayout*, LayoutHash> layoutCache;
};

#endif
