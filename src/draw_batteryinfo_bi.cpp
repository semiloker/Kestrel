#include "draw_batteryinfo_bi.h"
#include "app_identity_bi.h"

#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cwchar>

namespace
{
    const float TABBAR_H = 56.0f;
    const float PAD = 20.0f;
    const float CONTENT_MAX = 1040.0f;
    const float CARD_R = 10.0f;
    const float CARD_PAD = 16.0f;
    const float CARD_GAP = 12.0f;

    const float RAIL_W = 152.0f;
    const float RAIL_ITEM_H = 36.0f;
    const float RAIL_GAP = 2.0f;

    const float ROW_H = 52.0f;
    const float METRIC_ROW_H = 56.0f;

    const float CHIP_ROW_W = 54.0f;
    const float CHIP_GRAPH_W = 58.0f;
    const float CHIP_H = 28.0f;
    const float CHIP_R = 6.0f;

    const float TOGGLE_W = 44.0f;
    const float TOGGLE_H = 24.0f;
    const float KNOB_R = 9.0f;

    const float BTN_H = 32.0f;
    const float BTN_R = 6.0f;

    const float SWATCH = 32.0f;
    const float SWATCH_GAP = 12.0f;

    const int MASTER_TOGGLE_INDEX = -100;
    const float FOOTER_H = 30.0f;

    enum metric_color_bi
    {
        MC_NONE = -1,
        MC_CPU = 0,
        MC_GPU,
        MC_GPUMS,
        MC_POWER,
        MC_RAM,
        MC_COMMIT,
        MC_COUNT
    };

    const D2D1_COLOR_F METRIC_COLORS[MC_COUNT] = {
        D2D1::ColorF(0x82A0C2),
        D2D1::ColorF(0xA3BF8C),
        D2D1::ColorF(0xC98C8C),
        D2D1::ColorF(0xD9B585),
        D2D1::ColorF(0xBF99BF),
        D2D1::ColorF(0x8CBABD)};

    const wchar_t *GROUP_NAMES[] = {
        L"Overlay", L"Frame timing", L"GPU", L"CPU", L"Battery", L"Memory", L"Behavior"};

    const wchar_t *GROUP_DESC[] = {
        L"What the panel shows besides metrics.",
        L"Frame rate and per-frame GPU time, from ETW.",
        L"Adapter load and identity.",
        L"Processor load, package power and identity.",
        L"Battery readings mirrored into the overlay.",
        L"Physical, commit and virtual memory.",
        L"How Kestrel itself behaves."};

    const wchar_t *CORNER_NAMES[] = {
        L"Top left", L"Top right", L"Bottom left", L"Bottom right"};

    const wchar_t *PRESET_NAMES[] = {L"Minimal", L"Gaming", L"Everything"};
    const float PRESET_WIDTHS[] = {70.0f, 70.0f, 86.0f};

    std::wstring wfmt(const wchar_t *format, ...)
    {
        wchar_t buffer[512];
        va_list args;
        va_start(args, format);
        int written = _vsnwprintf(buffer, 511, format, args);
        va_end(args);

        if (written < 0)
            written = 511;
        buffer[written] = L'\0';

        return std::wstring(buffer);
    }

    std::wstring widen(const std::string &s)
    {
        return std::wstring(s.begin(), s.end());
    }

    std::wstring minutesText(int minutes)
    {
        if (minutes < 0)
            return L"\u2014";
        if (minutes < 60)
            return wfmt(L"%d m", minutes);
        return wfmt(L"%d h %02d m", minutes / 60, minutes % 60);
    }

    bool inRect(const D2D1_RECT_F &r, POINT p)
    {
        return p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom;
    }
}

draw_batteryinfo_bi::draw_batteryinfo_bi()
{
    accentColor = D2D1::ColorF(0x3366CC);
    nightMode = false;

    colorPalette = {
        D2D1::ColorF(0x3366CC), D2D1::ColorF(0x4C7FB8), D2D1::ColorF(0x5E81AC), D2D1::ColorF(0x4C8C8C),
        D2D1::ColorF(0x4F9E7F), D2D1::ColorF(0x6E9E56), D2D1::ColorF(0x8CA352), D2D1::ColorF(0xB58A3C),
        D2D1::ColorF(0xC27B45), D2D1::ColorF(0xC25E5E), D2D1::ColorF(0xB54A5E), D2D1::ColorF(0xA65293),
        D2D1::ColorF(0x8A63B5), D2D1::ColorF(0x6C63B5), D2D1::ColorF(0x5B6478), D2D1::ColorF(0x7A7A80)};
}

void draw_batteryinfo_bi::releaseDeviceResources()
{
    if (pBrush)
    {
        pBrush->Release();
        pBrush = nullptr;
    }

    rt = nullptr;
    hits.clear();
}

draw_batteryinfo_bi::~draw_batteryinfo_bi()
{
    releaseDeviceResources();
}

bool draw_batteryinfo_bi::updateBrushes(ID2D1HwndRenderTarget *pRT)
{
    if (nightMode)
    {
        pal.bg = D2D1::ColorF(0x0F0F12);
        pal.surface = D2D1::ColorF(0x17171C);
        pal.inset = D2D1::ColorF(0x202027);
        pal.border = D2D1::ColorF(0x2E2E36);
        pal.borderStrong = D2D1::ColorF(0x3A3A44);
        pal.text = D2D1::ColorF(0xEBEBEB);
        pal.muted = D2D1::ColorF(0xC7C7C7);
        pal.faint = D2D1::ColorF(0x8A8A94);
        pal.disabled = D2D1::ColorF(0x5C5C66);
        pal.trackOff = D2D1::ColorF(0x3A3A44);
        pal.knob = D2D1::ColorF(0xF0F0F2);
        pal.ok = D2D1::ColorF(0x7FB894);
        pal.warn = D2D1::ColorF(0xD9B585);
        pal.bad = D2D1::ColorF(0xC98C8C);
    }
    else
    {
        pal.bg = D2D1::ColorF(0xFAFAFA);
        pal.surface = D2D1::ColorF(0xFFFFFF);
        pal.inset = D2D1::ColorF(0xEFEFF2);
        pal.border = D2D1::ColorF(0xE4E4E8);
        pal.borderStrong = D2D1::ColorF(0xCCCCCC);
        pal.text = D2D1::ColorF(0x1A1A1A);
        pal.muted = D2D1::ColorF(0x666666);
        pal.faint = D2D1::ColorF(0x90909A);
        pal.disabled = D2D1::ColorF(0xB4B4BC);
        pal.trackOff = D2D1::ColorF(0xC8C8D0);
        pal.knob = D2D1::ColorF(0xFFFFFF);
        pal.ok = D2D1::ColorF(0x3F8F63);
        pal.warn = D2D1::ColorF(0xB07A2B);
        pal.bad = D2D1::ColorF(0xB04A4A);
    }

    if (!pBrush && pRT)
        pRT->CreateSolidColorBrush(pal.text, &pBrush);

    return pBrush != nullptr;
}

bool draw_batteryinfo_bi::initBrush(ID2D1HwndRenderTarget *pRT)
{
    return updateBrushes(pRT);
}

bool draw_batteryinfo_bi::clearBackground(ID2D1HwndRenderTarget *pRT)
{
    pRT->Clear(pal.bg);
    return true;
}

D2D1_COLOR_F draw_batteryinfo_bi::accentText() const
{
    if (!nightMode)
        return accentColor;

    D2D1_COLOR_F c;
    c.r = accentColor.r + (1.0f - accentColor.r) * 0.22f;
    c.g = accentColor.g + (1.0f - accentColor.g) * 0.22f;
    c.b = accentColor.b + (1.0f - accentColor.b) * 0.22f;
    c.a = 1.0f;
    return c;
}

D2D1_COLOR_F draw_batteryinfo_bi::tint(const D2D1_COLOR_F &c, float alpha) const
{
    D2D1_COLOR_F out = c;
    out.a = alpha;
    return out;
}

const D2D1_COLOR_F &draw_batteryinfo_bi::metricColor(int index) const
{
    if (index < 0 || index >= MC_COUNT)
        return pal.faint;
    return METRIC_COLORS[index];
}

void draw_batteryinfo_bi::beginFrame(ID2D1HwndRenderTarget *pRT)
{
    rt = pRT;

    D2D1_SIZE_F size = pRT->GetSize();
    viewWidth = size.width;
    viewHeight = size.height;

    hits.clear();
    footerActive = false;
    rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
}

float draw_batteryinfo_bi::contentLeft() const
{
    float usable = viewWidth - PAD * 2.0f;
    if (usable <= CONTENT_MAX)
        return PAD;
    return (viewWidth - CONTENT_MAX) * 0.5f;
}

float draw_batteryinfo_bi::contentRight() const
{
    float usable = viewWidth - PAD * 2.0f;
    if (usable <= CONTENT_MAX)
        return viewWidth - PAD;
    return (viewWidth + CONTENT_MAX) * 0.5f;
}

void draw_batteryinfo_bi::fillR(float l, float t, float r, float b, const D2D1_COLOR_F &c)
{
    pBrush->SetColor(c);
    D2D1_RECT_F rect = D2D1::RectF(l, t, r, b);
    rt->FillRectangle(&rect, pBrush);
}

void draw_batteryinfo_bi::fillRR(float l, float t, float r, float b, float radius,
                                 const D2D1_COLOR_F &c)
{
    pBrush->SetColor(c);
    D2D1_ROUNDED_RECT rr = {D2D1::RectF(l, t, r, b), radius, radius};
    rt->FillRoundedRectangle(rr, pBrush);
}

void draw_batteryinfo_bi::strokeRR(float l, float t, float r, float b, float radius,
                                   const D2D1_COLOR_F &c, float width)
{
    pBrush->SetColor(c);
    float inset = width * 0.5f;
    D2D1_ROUNDED_RECT rr = {D2D1::RectF(l + inset, t + inset, r - inset, b - inset),
                            radius, radius};
    rt->DrawRoundedRectangle(rr, pBrush, width);
}

void draw_batteryinfo_bi::fillEl(float cx, float cy, float radius, const D2D1_COLOR_F &c)
{
    pBrush->SetColor(c);
    D2D1_ELLIPSE e = {D2D1::Point2F(cx, cy), radius, radius};
    rt->FillEllipse(e, pBrush);
}

void draw_batteryinfo_bi::line(float x1, float y1, float x2, float y2,
                               const D2D1_COLOR_F &c, float width)
{
    pBrush->SetColor(c);
    rt->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), pBrush, width);
}

void draw_batteryinfo_bi::txt(IDWriteTextFormat *f, float l, float t, float r, float b,
                              const D2D1_COLOR_F &c, const std::wstring &s,
                              DWRITE_TEXT_ALIGNMENT align, bool clip)
{
    if (!f || s.empty())
        return;

    f->SetTextAlignment(align);
    pBrush->SetColor(c);
    rt->DrawText(s.c_str(), (UINT32)s.length(), f, D2D1::RectF(l, t, r, b), pBrush,
                 clip ? D2D1_DRAW_TEXT_OPTIONS_CLIP : D2D1_DRAW_TEXT_OPTIONS_NONE);
}

void draw_batteryinfo_bi::bar(float l, float t, float w, float h, float fraction,
                              const D2D1_COLOR_F &c)
{
    float radius = h * 0.5f;
    fillRR(l, t, l + w, t + h, radius, pal.inset);

    if (fraction <= 0.0f)
        return;
    if (fraction > 1.0f)
        fraction = 1.0f;

    float filled = w * fraction;

    if (filled < h)
    {
        if (filled < 2.0f)
            filled = 2.0f;
        fillR(l, t, l + filled, t + h, c);
    }
    else
    {
        fillRR(l, t, l + filled, t + h, radius, c);
    }
}

void draw_batteryinfo_bi::card(float l, float t, float r, float b)
{
    fillRR(l, t, r, b, CARD_R, pal.surface);
    strokeRR(l, t, r, b, CARD_R, pal.border, 1.0f);
}

void draw_batteryinfo_bi::eyebrow(init_dwrite_bi *dw, float x, float y, const std::wstring &s)
{
    txt(dw->pTextFormatMicro, x, y, x + 400.0f, y + 13.0f, pal.faint, s);
}

void draw_batteryinfo_bi::statCell(init_dwrite_bi *dw, float x, float y, float w,
                                   const std::wstring &label, const std::wstring &value,
                                   bool missing)
{
    txt(dw->pTextFormatMicro, x, y, x + w, y + 13.0f, pal.faint, label);
    txt(dw->pTextFormatStrong, x, y + 17.0f, x + w, y + 36.0f,
        missing ? pal.faint : pal.text, value);
}

void draw_batteryinfo_bi::toggle(float x, float y, bool on, bool dimmed)
{
    if (dimmed)
    {
        fillRR(x, y, x + TOGGLE_W, y + TOGGLE_H, TOGGLE_H * 0.5f, pal.inset);
        strokeRR(x, y, x + TOGGLE_W, y + TOGGLE_H, TOGGLE_H * 0.5f, pal.borderStrong, 1.0f);
    }
    else
    {
        fillRR(x, y, x + TOGGLE_W, y + TOGGLE_H, TOGGLE_H * 0.5f,
               on ? accentColor : pal.trackOff);
    }

    float knobX = on ? (x + TOGGLE_W - 3.0f - KNOB_R) : (x + 3.0f + KNOB_R);
    fillEl(knobX, y + TOGGLE_H * 0.5f, KNOB_R, dimmed ? pal.disabled : pal.knob);
}

void draw_batteryinfo_bi::chip(init_dwrite_bi *dw, float x, float y, float w,
                               const std::wstring &label, bool on, bool dimmed)
{
    if (dimmed)
    {
        fillRR(x, y, x + w, y + CHIP_H, CHIP_R, pal.inset);
        strokeRR(x, y, x + w, y + CHIP_H, CHIP_R, pal.border, 1.0f);
        txt(dw->pTextFormatMicro, x, y, x + w, y + CHIP_H, pal.disabled, label,
            DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    else if (on)
    {
        fillRR(x, y, x + w, y + CHIP_H, CHIP_R, accentColor);
        txt(dw->pTextFormatMicro, x, y, x + w, y + CHIP_H, D2D1::ColorF(0xFFFFFF), label,
            DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    else
    {
        fillRR(x, y, x + w, y + CHIP_H, CHIP_R, pal.inset);
        strokeRR(x, y, x + w, y + CHIP_H, CHIP_R, pal.border, 1.0f);
        txt(dw->pTextFormatMicro, x, y, x + w, y + CHIP_H, pal.faint, label,
            DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

void draw_batteryinfo_bi::button(init_dwrite_bi *dw, float x, float y, float w,
                                 const std::wstring &label, bool primary, bool hot)
{
    if (primary)
    {
        fillRR(x, y, x + w, y + BTN_H, BTN_R, accentColor);
        txt(dw->pTextFormatSmall, x, y, x + w, y + BTN_H, D2D1::ColorF(0xFFFFFF), label,
            DWRITE_TEXT_ALIGNMENT_CENTER);

        if (hot)
            hoverRing(x, y, x + w, y + BTN_H, BTN_R);
    }
    else
    {
        if (hot)
            hoverFill(x, y, x + w, y + BTN_H, BTN_R);

        strokeRR(x, y, x + w, y + BTN_H, BTN_R, hot ? pal.faint : pal.border, 1.0f);
        txt(dw->pTextFormatSmall, x, y, x + w, y + BTN_H, pal.text, label,
            DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

void draw_batteryinfo_bi::logoMark(float x, float y, float size)
{
    ID2D1Factory *factory = nullptr;
    rt->GetFactory(&factory);
    if (!factory)
        return;

    ID2D1PathGeometry *geometry = nullptr;
    if (FAILED(factory->CreatePathGeometry(&geometry)) || !geometry)
    {
        factory->Release();
        return;
    }

    ID2D1GeometrySink *sink = nullptr;
    if (FAILED(geometry->Open(&sink)) || !sink)
    {
        geometry->Release();
        factory->Release();
        return;
    }

    float s = size / 256.0f;
    float oy = y + 10.0f * s;

    struct wing
    {
        float cx1, cy1, ex1, ey1;
        float cx2, cy2, ex2, ey2;
    };

    const wing wings[2] = {
        {198.0f, 82.0f, 244.0f, 182.0f, 172.0f, 126.0f, 128.0f, 108.0f},
        {58.0f, 82.0f, 12.0f, 182.0f, 84.0f, 126.0f, 128.0f, 108.0f}};

    for (int i = 0; i < 2; ++i)
    {
        const wing &w = wings[i];

        sink->BeginFigure(D2D1::Point2F(x + 128.0f * s, oy + 54.0f * s),
                          D2D1_FIGURE_BEGIN_FILLED);

        sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(
            D2D1::Point2F(x + w.cx1 * s, oy + w.cy1 * s),
            D2D1::Point2F(x + w.ex1 * s, oy + w.ey1 * s)));

        sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(
            D2D1::Point2F(x + w.cx2 * s, oy + w.cy2 * s),
            D2D1::Point2F(x + w.ex2 * s, oy + w.ey2 * s)));

        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    }

    sink->Close();
    sink->Release();

    pBrush->SetColor(pal.text);
    rt->FillGeometry(geometry, pBrush);

    geometry->Release();
    factory->Release();
}

void draw_batteryinfo_bi::hoverRing(float l, float t, float r, float b, float radius)
{
    strokeRR(l - 2.0f, t - 2.0f, r + 2.0f, b + 2.0f, radius + 2.0f, accentText(), 1.5f);
}

void draw_batteryinfo_bi::hoverFill(float l, float t, float r, float b, float radius)
{
    fillRR(l, t, r, b, radius, tint(pal.text, nightMode ? 0.07f : 0.045f));
}

void draw_batteryinfo_bi::pushHit(const D2D1_RECT_F &rect, int kind, int index, bool *state)
{
    hit_bi h;
    h.rect = rect;
    h.kind = kind;
    h.index = index;
    h.state = state;
    hits.push_back(h);
}

void draw_batteryinfo_bi::setTab(selected_option tab)
{
    if (tab == selectedTab)
        return;

    tabScroll[selectedTab] = scrollOffsetY;
    selectedTab = tab;
    scrollOffsetY = tabScroll[tab];
    contentHeight = 0.0f;
}

void draw_batteryinfo_bi::setSettingsGroup(int g)
{
    if (g < 0 || g >= GROUP_COUNT || g == settingsGroup)
        return;

    settingsGroup = g;
    scrollOffsetY = 0.0f;
    contentHeight = 0.0f;
}

void draw_batteryinfo_bi::clampScroll()
{
    float maxScroll = contentHeight - viewHeight;
    if (maxScroll < 0.0f)
        maxScroll = 0.0f;

    if (scrollOffsetY > maxScroll)
        scrollOffsetY = maxScroll;
    if (scrollOffsetY < 0.0f)
        scrollOffsetY = 0.0f;
}

void draw_batteryinfo_bi::scrollBy(float amount)
{
    scrollOffsetY += amount;
    clampScroll();
}

const draw_batteryinfo_bi::hit_bi *draw_batteryinfo_bi::findHit(POINT cursorPos) const
{
    for (size_t i = 0; i < hits.size(); ++i)
    {
        const hit_bi &h = hits[i];

        if (h.kind != HIT_TAB && h.rect.top < TABBAR_H)
            continue;

        if (inRect(h.rect, cursorPos))
            return &h;
    }
    return nullptr;
}

bool draw_batteryinfo_bi::isHovered(int kind, int index) const
{
    return hoverKind == kind && hoverIndex == index;
}

bool draw_batteryinfo_bi::setHover(POINT cursorPos)
{
    if (scrollDragging)
        return false;

    const hit_bi *h = findHit(cursorPos);

    int kind = h ? h->kind : (int)HIT_NONE;
    int index = h ? h->index : -1;

    if (kind == hoverKind && index == hoverIndex)
        return false;

    hoverKind = kind;
    hoverIndex = index;
    return true;
}

bool draw_batteryinfo_bi::clearHover()
{
    if (hoverKind == HIT_NONE && hoverIndex == -1)
        return false;

    hoverKind = HIT_NONE;
    hoverIndex = -1;
    return true;
}

bool draw_batteryinfo_bi::isOverInteractive(POINT cursorPos) const
{
    if (findHit(cursorPos))
        return true;

    return scrollActive && cursorPos.x >= viewWidth - 16.0f &&
           cursorPos.y >= scrollTrackTop &&
           cursorPos.y <= scrollTrackTop + scrollTrackHeight;
}

bool draw_batteryinfo_bi::beginScrollDrag(POINT cursorPos)
{
    if (!scrollActive)
        return false;

    if (cursorPos.x < viewWidth - 16.0f || cursorPos.x > viewWidth)
        return false;

    if (cursorPos.y < scrollTrackTop || cursorPos.y > scrollTrackTop + scrollTrackHeight)
        return false;

    if (cursorPos.y >= scrollThumbRect.top && cursorPos.y <= scrollThumbRect.bottom)
    {
        scrollDragging = true;
        dragStartY = (float)cursorPos.y;
        dragStartOffset = scrollOffsetY;
        return true;
    }

    float page = (viewHeight - TABBAR_H) * 0.85f;
    scrollBy(cursorPos.y < scrollThumbRect.top ? -page : page);
    return true;
}

void draw_batteryinfo_bi::updateScrollDrag(POINT cursorPos)
{
    if (!scrollDragging)
        return;

    float usable = scrollTrackHeight - scrollThumbHeight;
    if (usable <= 0.0f)
        return;

    float maxScroll = contentHeight - viewHeight;
    if (maxScroll <= 0.0f)
        return;

    float delta = ((float)cursorPos.y - dragStartY) * (maxScroll / usable);

    scrollOffsetY = dragStartOffset + delta;
    clampScroll();
}

void draw_batteryinfo_bi::endScrollDrag()
{
    scrollDragging = false;
}

void draw_batteryinfo_bi::drawFooter(init_dwrite_bi *dw, const std::wstring &text)
{
    float top = viewHeight - FOOTER_H;

    fillR(0.0f, top, viewWidth, viewHeight, pal.bg);
    line(0.0f, top, viewWidth, top, pal.border, 1.0f);

    txt(dw->pTextFormatMicro, 0.0f, top, viewWidth, viewHeight, pal.faint, text,
        DWRITE_TEXT_ALIGNMENT_CENTER);

    footerActive = true;
}

void draw_batteryinfo_bi::drawScrollbar()
{
    scrollActive = contentHeight > viewHeight;

    if (!scrollActive)
    {
        scrollThumbRect = D2D1::RectF(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    scrollTrackTop = TABBAR_H + 4.0f;
    scrollTrackHeight = viewHeight - scrollTrackTop - 4.0f -
                        (footerActive ? FOOTER_H : 0.0f);

    float visible = (viewHeight - TABBAR_H) / (contentHeight - TABBAR_H);
    if (visible > 1.0f)
        visible = 1.0f;

    scrollThumbHeight = scrollTrackHeight * visible;
    if (scrollThumbHeight < 32.0f)
        scrollThumbHeight = 32.0f;

    float maxScroll = contentHeight - viewHeight;
    float progress = (maxScroll > 0.0f) ? (scrollOffsetY / maxScroll) : 0.0f;
    float barY = scrollTrackTop + progress * (scrollTrackHeight - scrollThumbHeight);

    float width = scrollDragging ? 7.0f : 5.0f;
    float x = viewWidth - 5.0f - width;

    scrollThumbRect = D2D1::RectF(x, barY, x + width, barY + scrollThumbHeight);

    fillRR(x + width * 0.5f - 1.5f, scrollTrackTop, x + width * 0.5f + 1.5f,
           scrollTrackTop + scrollTrackHeight, 1.5f, pal.inset);

    fillRR(x, barY, x + width, barY + scrollThumbHeight, width * 0.5f,
           scrollDragging ? accentText() : pal.disabled);
}

void draw_batteryinfo_bi::drawTabBar(ID2D1HwndRenderTarget *pRT, init_dwrite_bi *dw)
{
    rt = pRT;

    D2D1_SIZE_F size = pRT->GetSize();
    viewWidth = size.width;

    fillR(0.0f, 0.0f, viewWidth, TABBAR_H, pal.bg);

    logoMark(PAD, 15.0f, 26.0f);
    txt(dw->pTextFormatStrong, PAD + 34.0f, 19.0f, PAD + 110.0f, 37.0f, pal.text, L"Kestrel");

    const wchar_t *labels[TAB_COUNT] = {L"Battery", L"Settings", L"Capture",
                                        L"Appearance", L"About"};
    const float widths[TAB_COUNT] = {48.0f, 53.0f, 52.0f, 74.0f, 40.0f};

    float x = 132.0f;
    D2D1_COLOR_F active = accentText();

    for (int i = 0; i < TAB_COUNT; ++i)
    {
        bool on = (selectedTab == (selected_option)i);
        bool hot = isHovered(HIT_TAB, i);

        txt(dw->pTextFormatValue, x, 19.0f, x + widths[i] + 6.0f, 37.0f,
            on ? active : (hot ? pal.text : pal.muted), labels[i]);

        if (on)
            fillRR(x, 54.0f, x + widths[i], 56.0f, 1.0f, active);
        else if (hot)
            fillRR(x, 54.0f, x + widths[i], 56.0f, 1.0f, pal.borderStrong);

        pushHit(D2D1::RectF(x - 8.0f, 0.0f, x + widths[i] + 8.0f, TABBAR_H), HIT_TAB, i, nullptr);

        x += widths[i] + 22.0f;
    }

    line(0.0f, TABBAR_H, viewWidth, TABBAR_H, pal.borderStrong, 1.0f);
}

void draw_batteryinfo_bi::drawBatteryTab(ID2D1HwndRenderTarget *pRT, init_dwrite_bi *dw,
                                         batteryinfo_bi *bi)
{
    beginFrame(pRT);

    float L = contentLeft();
    float R = contentRight();
    float IX = L + CARD_PAD;
    float IR = R - CARD_PAD;
    float IW = IR - IX;
    float shift = -scrollOffsetY;

    if (!bi || !bi->present)
    {
        float top = TABBAR_H + PAD + shift;
        card(L, top, R, top + 96.0f);
        txt(dw->pTextFormatStrong, IX, top + 24.0f, IR, top + 44.0f, pal.text,
            L"No battery detected");
        txt(dw->pTextFormatLabel, IX, top + 50.0f, IR, top + 68.0f, pal.muted,
            L"The battery device did not answer. Kestrel still reads CPU, GPU and memory.");
        contentHeight = top + 96.0f + scrollOffsetY + PAD;
        clampScroll();
        return;
    }

    bool charging = bi->info_1s.charging;
    bool discharging = bi->info_1s.discharging;

    const D2D1_COLOR_F &stateColor = charging ? pal.ok : (discharging ? pal.warn : pal.muted);
    std::wstring stateText = charging ? L"Charging" : (discharging ? L"Discharging" : L"Idle");

    float y = TABBAR_H + PAD + shift;

    card(L, y, R, y + 136.0f);
    eyebrow(dw, IX, y + 16.0f, L"CHARGE");

    if (bi->info_1s.chargeValid)
    {
        std::wstring whole = wfmt(L"%.0f", bi->info_1s.chargePercent);
        txt(dw->pTextFormatDisplay, IX, y + 31.0f, IX + 120.0f, y + 73.0f, pal.text, whole);

        float offset = (float)whole.length() * 19.0f + 4.0f;
        txt(dw->pTextFormatHeader, IX + offset, y + 50.0f, IX + offset + 30.0f, y + 72.0f,
            pal.muted, L"%");
    }
    else
    {
        txt(dw->pTextFormatDisplay, IX, y + 31.0f, IX + 120.0f, y + 73.0f, pal.faint, L"\u2014");
    }

    float pillW = 100.0f;
    fillRR(IR - pillW, y + 18.0f, IR, y + 42.0f, 12.0f, tint(stateColor, 0.16f));
    txt(dw->pTextFormatLabel, IR - pillW, y + 18.0f, IR, y + 42.0f, stateColor, stateText,
        DWRITE_TEXT_ALIGNMENT_CENTER);

    if (bi->info_1s.rateValid)
    {
        txt(dw->pTextFormatHeader, IR - 200.0f, y + 47.0f, IR, y + 73.0f, pal.text,
            wfmt(L"%.1f W", bi->info_1s.rateW), DWRITE_TEXT_ALIGNMENT_TRAILING);
    }
    else
    {
        txt(dw->pTextFormatHeader, IR - 200.0f, y + 47.0f, IR, y + 73.0f, pal.faint, L"\u2014",
            DWRITE_TEXT_ALIGNMENT_TRAILING);
    }

    float fraction = bi->info_1s.chargeValid ? (float)(bi->info_1s.chargePercent / 100.0) : 0.0f;
    bar(IX, y + 91.0f, IW, 10.0f, fraction, accentColor);

    if (bi->info_1s.remainingValid && bi->info_static.capacityValid)
    {
        txt(dw->pTextFormatLabel, IX, y + 109.0f, IX + 260.0f, y + 124.0f, pal.muted,
            wfmt(L"%.1f Wh of %.1f Wh", bi->info_1s.remainingWh, bi->info_static.fullChargedWh));
    }

    int left = discharging ? bi->info_10s.minutesToEmpty : bi->info_10s.minutesToFull;
    if (left >= 0)
    {
        txt(dw->pTextFormatLabel, IR - 220.0f, y + 109.0f, IR, y + 124.0f, pal.muted,
            minutesText(left) + (discharging ? L" left" : L" to full"),
            DWRITE_TEXT_ALIGNMENT_TRAILING);
    }

    y += 136.0f + CARD_GAP;

    card(L, y, R, y + 108.0f);
    eyebrow(dw, IX, y + 16.0f, L"HEALTH");

    if (bi->info_static.wearValid)
    {
        double health = 100.0 - bi->info_static.wearPercent;

        const D2D1_COLOR_F &wearColor = (bi->info_static.wearPercent < 15.0)
                                            ? pal.ok
                                            : (bi->info_static.wearPercent < 30.0 ? pal.warn : pal.bad);

        txt(dw->pTextFormatHeader, IX, y + 32.0f, IX + 70.0f, y + 56.0f, pal.text,
            wfmt(L"%.0f%%", health));
        txt(dw->pTextFormatSmall, IX + 52.0f, y + 37.0f, IX + 260.0f, y + 55.0f, pal.muted,
            L"of design capacity");
        txt(dw->pTextFormatLabel, IR - 160.0f, y + 37.0f, IR, y + 55.0f, wearColor,
            wfmt(L"%.0f%% wear", bi->info_static.wearPercent), DWRITE_TEXT_ALIGNMENT_TRAILING);

        bar(IX, y + 67.0f, IW, 8.0f, (float)(health / 100.0), wearColor);
    }
    else
    {
        txt(dw->pTextFormatSmall, IX, y + 40.0f, IR, y + 58.0f, pal.faint,
            L"This battery does not report a design capacity.");
    }

    if (bi->info_static.cycleCountValid)
    {
        txt(dw->pTextFormatLabel, IX, y + 85.0f, IX + 200.0f, y + 100.0f, pal.muted,
            wfmt(L"%d cycles", bi->info_static.cycleCount));
    }
    else
    {
        txt(dw->pTextFormatLabel, IX, y + 85.0f, IX + 200.0f, y + 100.0f, pal.faint,
            L"cycle count unavailable");
    }

    if (bi->info_static.capacityValid)
    {
        txt(dw->pTextFormatLabel, IR - 280.0f, y + 85.0f, IR, y + 100.0f, pal.muted,
            wfmt(L"%.1f Wh design \u00B7 %.1f Wh full",
                 bi->info_static.designedWh, bi->info_static.fullChargedWh),
            DWRITE_TEXT_ALIGNMENT_TRAILING);
    }

    y += 108.0f + CARD_GAP;

    struct detail_cell
    {
        const wchar_t *label;
        std::wstring value;
        bool missing;
    };

    detail_cell details[6] = {
        {L"CHEMISTRY", widen(bi->info_static.Chemistry), false},
        {L"POWER STATE", bi->info_1s.onLine ? L"On mains" : L"On battery", false},
        {L"VOLTAGE",
         bi->info_1s.voltageValid ? wfmt(L"%.2f V", bi->info_1s.voltageV) : L"\u2014",
         !bi->info_1s.voltageValid},
        {L"POWER DRAW",
         bi->info_1s.rateValid ? wfmt(L"%.2f W", bi->info_1s.rateW) : L"\u2014",
         !bi->info_1s.rateValid},
        {L"TIME TO EMPTY", minutesText(bi->info_10s.minutesToEmpty),
         bi->info_10s.minutesToEmpty < 0},
        {L"TIME TO FULL", minutesText(bi->info_10s.minutesToFull),
         bi->info_10s.minutesToFull < 0}};

    int cols = (IW > 660.0f) ? 3 : 2;
    int detailRows = (6 + cols - 1) / cols;

    float gap = 16.0f;
    float colW = (IW - gap * (float)(cols - 1)) / (float)cols;
    float detailH = 41.0f + (float)detailRows * 48.0f + 14.0f;

    card(L, y, R, y + detailH);
    eyebrow(dw, IX, y + 16.0f, L"DETAILS");

    for (int i = 0; i < 6; ++i)
    {
        float cx = IX + (float)(i % cols) * (colW + gap);
        float cy = y + 41.0f + (float)(i / cols) * 48.0f;

        statCell(dw, cx, cy, colW, details[i].label, details[i].value, details[i].missing);
    }

    y += detailH + CARD_GAP;

    card(L, y, R, y + 124.0f);
    eyebrow(dw, IX, y + 16.0f, L"ALERTS");

    bool alerts = bi->info_static.alertsValid;

    txt(dw->pTextFormatValue, IX, y + 41.0f, IX + 240.0f, y + 61.0f, pal.text, L"Low battery");
    txt(dw->pTextFormatSmall, IR - 160.0f, y + 41.0f, IR, y + 61.0f,
        alerts ? pal.muted : pal.faint,
        alerts ? wfmt(L"%.1f Wh", bi->info_static.alert1Wh) : L"Not set",
        DWRITE_TEXT_ALIGNMENT_TRAILING);

    line(IX, y + 72.0f, IR, y + 72.0f, pal.border, 1.0f);

    txt(dw->pTextFormatValue, IX, y + 83.0f, IX + 240.0f, y + 103.0f, pal.text,
        L"Critical battery");
    txt(dw->pTextFormatSmall, IR - 160.0f, y + 83.0f, IR, y + 103.0f,
        alerts ? pal.muted : pal.faint,
        alerts ? wfmt(L"%.1f Wh", bi->info_static.alert2Wh) : L"Not set",
        DWRITE_TEXT_ALIGNMENT_TRAILING);

    y += 124.0f + PAD;

    contentHeight = y + scrollOffsetY + FOOTER_H;
    clampScroll();

    drawFooter(dw, L"Windows owns these thresholds. Kestrel only reads them.");
    drawScrollbar();
}

int draw_batteryinfo_bi::buildGroupRows(int group, overlay_bi *ov, resource_usage_bi *ru,
                                        batteryinfo_bi *bi, std::vector<row_bi> &out) const
{
    out.clear();

    if (!ov || !ru || !bi)
        return 0;

    bool frameOk = ov->hud.metrics[HUD_M_FPS].available;
    bool powerOk = ru->cpuInfo.packagePowerAvailable;
    row_bi r;

    switch (group)
    {
    case GROUP_OVERLAY:
        r = {L"Display info row", &ov->hud.showDisplay, nullptr, MC_NONE, true,
             L"Monitor, resolution and refresh at the top"};
        out.push_back(r);
        r = {L"Frame time percentiles", &ov->hud.showLows, nullptr, MC_NONE, frameOk,
             L"Worst-case 1% and 0.1% low FPS (Low:)"};
        out.push_back(r);
        break;

    case GROUP_FRAME:
        r = {L"Frame rate", &ov->hud.metrics[HUD_M_FPS].show, nullptr, MC_CPU, frameOk,
             L"Frames per second (FPS:)"};
        out.push_back(r);
        r = {L"Frame interval", &ov->hud.metrics[HUD_M_PRE].show,
             &ov->hud.metrics[HUD_M_PRE].graphed, MC_CPU, frameOk,
             L"Milliseconds between frames (Pre:)"};
        out.push_back(r);
        r = {L"GPU milliseconds", &ov->hud.metrics[HUD_M_GPUMS].show,
             &ov->hud.metrics[HUD_M_GPUMS].graphed, MC_GPUMS, frameOk,
             L"GPU render time per frame (GPU: ms)"};
        out.push_back(r);
        r = {L"CPU or GPU bound", &ov->hud.showBottleneck, nullptr, MC_NONE, frameOk,
             L"Which one is capping the frame rate (Bnd:)"};
        out.push_back(r);
        break;

    case GROUP_GPU:
        r = {L"Load", &ru->gpuInfo.show_gpuLoad, &ov->hud.metrics[HUD_M_GPU].graphed, MC_GPU, true,
             L"GPU busy percentage (GPU: %)"};
        out.push_back(r);
        r = {L"Video memory", &ru->gpuInfo.show_vram, nullptr, MC_GPU,
             ru->gpuInfo.vramTotalMB > 0.0, L"VRAM used and total (VRM:)"};
        out.push_back(r);
        r = {L"Power draw", &ru->gpuInfo.show_gpuPower,
             &ov->hud.metrics[HUD_M_GPUW].graphed, MC_POWER, ru->gpuInfo.gpuPowerAvailable,
             L"GPU watts from the Energy Meter (GPW:)"};
        out.push_back(r);
        r = {L"Adapter name", &ru->gpuInfo.show_gpuName, nullptr, MC_NONE, true,
             L"Graphics card model"};
        out.push_back(r);
        break;

    case GROUP_CPU:
        r = {L"Load", &ru->cpuInfo.show_UsagePercent, &ov->hud.metrics[HUD_M_CPU].graphed,
             MC_CPU, true, L"Total CPU utilisation (CPU: %)"};
        out.push_back(r);
        r = {L"Package power", &ru->cpuInfo.show_packagePower,
             &ov->hud.metrics[HUD_M_CPUW].graphed, MC_POWER, powerOk,
             L"CPU package watts (CPW:)"};
        out.push_back(r);
        r = {L"Frames per watt", &ov->hud.showEfficiency, nullptr, MC_POWER,
             frameOk && powerOk, L"FPS delivered per CPU watt (Eff:)"};
        out.push_back(r);
        r = {L"Per-core load", &ru->cpuInfo.show_CoreUsagePercents, nullptr, MC_NONE, true,
             L"Utilisation of each logical core"};
        out.push_back(r);
        r = {L"Processor name", &ru->cpuInfo.show_cpuName, nullptr, MC_NONE, true,
             L"CPU model, shown above the adapter"};
        out.push_back(r);
        r = {L"Architecture", &ru->cpuInfo.show_architecture, nullptr, MC_NONE, true,
             L"Instruction set, e.g. x64 (Arch:)"};
        out.push_back(r);
        break;

    case GROUP_BATTERY:
        r = {L"Voltage", &bi->info_1s.Voltage_, nullptr, MC_NONE, bi->present,
             L"Battery terminal voltage"};
        out.push_back(r);
        r = {L"Power draw", &bi->info_1s.Rate_, nullptr, MC_NONE, bi->present,
             L"Charge or discharge watts"};
        out.push_back(r);
        r = {L"Power state", &bi->info_1s.PowerState_, nullptr, MC_NONE, bi->present,
             L"Charging, discharging or idle"};
        out.push_back(r);
        r = {L"Remaining capacity", &bi->info_1s.RemainingCapacity_, nullptr, MC_NONE, bi->present,
             L"Energy left in watt-hours"};
        out.push_back(r);
        r = {L"Charge level", &bi->info_1s.ChargeLevel_, nullptr, MC_NONE, bi->present,
             L"Battery percentage"};
        out.push_back(r);
        r = {L"Time remaining", &bi->info_10s.TimeRemaining_, nullptr, MC_NONE, bi->present,
             L"Estimated time to empty or full"};
        out.push_back(r);
        r = {L"Charger deficit warning", &ov->hud.showChargerDeficit, nullptr, MC_NONE,
             bi->present, L"Warns when the charger cannot keep up (Chg:)"};
        out.push_back(r);
        break;

    case GROUP_MEMORY:
        r = {L"Load", &ru->ramInfo.show_dwMemoryLoad, &ov->hud.metrics[HUD_M_RAM].graphed,
             MC_RAM, true, L"Physical RAM in use (RAM: %)"};
        out.push_back(r);
        r = {L"Commit charge", &ru->ramInfo.show_ullTotalPageFile,
             &ov->hud.metrics[HUD_M_COMMIT].graphed, MC_COMMIT, true,
             L"Committed memory against the limit (Cmt:)"};
        out.push_back(r);
        r = {L"Total physical", &ru->ramInfo.show_ullTotalPhys, nullptr, MC_NONE, true,
             L"Installed RAM"};
        out.push_back(r);
        r = {L"Available physical", &ru->ramInfo.show_ullAvailPhys, nullptr, MC_NONE, true,
             L"Free RAM"};
        out.push_back(r);
        r = {L"Available page file", &ru->ramInfo.show_ullAvailPageFile, nullptr, MC_NONE, true,
             L"Free page-file space"};
        out.push_back(r);
        r = {L"Total virtual", &ru->ramInfo.show_ullTotalVirtual, nullptr, MC_NONE, true,
             L"Size of the address space"};
        out.push_back(r);
        r = {L"Available virtual", &ru->ramInfo.show_ullAvailVirtual, nullptr, MC_NONE, true,
             L"Free address space"};
        out.push_back(r);
        r = {L"Extended virtual", &ru->ramInfo.show_ullAvailExtendedVirtual, nullptr,
             MC_NONE, true, L"Free extended address space"};
        out.push_back(r);
        break;

    case GROUP_BEHAVIOR:
        r = {L"Start with Windows", &ru->start_With_Windows, nullptr, MC_NONE, true,
             L"Launch at sign-in, into the tray if 'Minimize to tray' is on"};
        out.push_back(r);
        r = {L"Run as administrator", &ru->start_As_Admin, nullptr, MC_NONE, true,
             L"Needed for frame timing (ETW)"};
        out.push_back(r);
        r = {L"Minimize to tray", &ru->minimize_To_Tray, nullptr, MC_NONE, true,
             L"Hide to the tray instead of the taskbar"};
        out.push_back(r);
        r = {L"Exit on ESC", &ru->exit_on_key_esc, nullptr, MC_NONE, true,
             L"Close the window when you press Escape"};
        out.push_back(r);
        break;
    }

    return (int)out.size();
}

void draw_batteryinfo_bi::applyPreset(int preset, overlay_bi *ov, resource_usage_bi *ru,
                                      batteryinfo_bi *bi)
{
    if (!ov || !ru || !bi)
        return;

    for (int g = GROUP_OVERLAY; g < GROUP_BEHAVIOR; ++g)
    {
        std::vector<row_bi> rows;
        buildGroupRows(g, ov, ru, bi, rows);

        for (size_t i = 0; i < rows.size(); ++i)
        {
            if (rows[i].show)
                *rows[i].show = false;
            if (rows[i].graph)
                *rows[i].graph = false;
        }
    }

    ov->hud.showDisplay = true;
    ov->hud.showChargerDeficit = true;

    if (preset == 0)
    {
        ov->hud.metrics[HUD_M_FPS].show = true;
        ru->cpuInfo.show_UsagePercent = true;
        ru->gpuInfo.show_gpuLoad = true;
    }
    else if (preset == 1)
    {
        ov->hud.showLows = true;
        ov->hud.showBottleneck = true;
        ov->hud.showEfficiency = true;
        ov->hud.metrics[HUD_M_FPS].show = true;
        ov->hud.metrics[HUD_M_PRE].show = true;
        ov->hud.metrics[HUD_M_PRE].graphed = true;
        ov->hud.metrics[HUD_M_GPUMS].show = true;
        ov->hud.metrics[HUD_M_GPUMS].graphed = true;
        ru->cpuInfo.show_UsagePercent = true;
        ov->hud.metrics[HUD_M_CPU].graphed = true;
        ru->cpuInfo.show_packagePower = true;
        ov->hud.metrics[HUD_M_CPUW].graphed = true;
        ru->gpuInfo.show_gpuLoad = true;
        ov->hud.metrics[HUD_M_GPU].graphed = true;
        ru->gpuInfo.show_gpuName = true;
        ru->ramInfo.show_dwMemoryLoad = true;
        ru->ramInfo.show_ullTotalPhys = true;
    }
    else
    {
        for (int g = GROUP_OVERLAY; g < GROUP_BEHAVIOR; ++g)
        {
            std::vector<row_bi> rows;
            buildGroupRows(g, ov, ru, bi, rows);

            for (size_t i = 0; i < rows.size(); ++i)
            {
                if (rows[i].show)
                    *rows[i].show = true;
                if (rows[i].graph)
                    *rows[i].graph = true;
            }
        }
    }
}

void draw_batteryinfo_bi::snapshotRows(overlay_bi *ov, resource_usage_bi *ru,
                                       batteryinfo_bi *bi, std::vector<bool> &out) const
{
    out.clear();

    for (int g = GROUP_OVERLAY; g < GROUP_BEHAVIOR; ++g)
    {
        std::vector<row_bi> rows;
        buildGroupRows(g, ov, ru, bi, rows);

        for (size_t i = 0; i < rows.size(); ++i)
        {
            out.push_back(rows[i].show ? *rows[i].show : false);
            if (rows[i].graph)
                out.push_back(*rows[i].graph);
        }
    }
}

void draw_batteryinfo_bi::restoreRows(overlay_bi *ov, resource_usage_bi *ru,
                                      batteryinfo_bi *bi, const std::vector<bool> &in) const
{
    size_t index = 0;

    for (int g = GROUP_OVERLAY; g < GROUP_BEHAVIOR; ++g)
    {
        std::vector<row_bi> rows;
        buildGroupRows(g, ov, ru, bi, rows);

        for (size_t i = 0; i < rows.size(); ++i)
        {
            if (index >= in.size())
                return;

            if (rows[i].show)
                *rows[i].show = in[index];
            ++index;

            if (rows[i].graph)
            {
                if (index >= in.size())
                    return;
                *rows[i].graph = in[index];
                ++index;
            }
        }
    }
}

std::string draw_batteryinfo_bi::captureLayout(overlay_bi *ov, resource_usage_bi *ru,
                                               batteryinfo_bi *bi) const
{
    if (!ov || !ru || !bi)
        return std::string();

    std::vector<bool> bits;
    snapshotRows(ov, ru, bi, bits);

    char hdr[16];
    snprintf(hdr, sizeof(hdr), "%d:", (int)bits.size());

    std::string out = hdr;
    for (size_t i = 0; i < bits.size(); ++i)
        out += bits[i] ? '1' : '0';

    return out;
}

void draw_batteryinfo_bi::applyLayout(const std::string &s, overlay_bi *ov,
                                      resource_usage_bi *ru, batteryinfo_bi *bi)
{
    if (!ov || !ru || !bi)
        return;

    size_t colon = s.find(':');
    if (colon == std::string::npos)
        return;

    int count = atoi(s.substr(0, colon).c_str());
    std::string bits = s.substr(colon + 1);

    if (count <= 0 || (int)bits.size() < count)
        return;

    std::vector<bool> expected;
    snapshotRows(ov, ru, bi, expected);

    if ((int)expected.size() != count)
        return;

    std::vector<bool> in;
    in.reserve((size_t)count);
    for (int i = 0; i < count; ++i)
        in.push_back(bits[(size_t)i] == '1');

    restoreRows(ov, ru, bi, in);
}

int draw_batteryinfo_bi::detectPreset(overlay_bi *ov, resource_usage_bi *ru,
                                      batteryinfo_bi *bi)
{
    if (!ov || !ru || !bi)
        return -1;

    std::vector<bool> original;
    snapshotRows(ov, ru, bi, original);

    int found = -1;

    for (int p = 0; p < 3; ++p)
    {
        applyPreset(p, ov, ru, bi);

        std::vector<bool> candidate;
        snapshotRows(ov, ru, bi, candidate);

        restoreRows(ov, ru, bi, original);

        if (candidate == original)
        {
            found = p;
            break;
        }
    }

    return found;
}

void draw_batteryinfo_bi::drawPanelText(init_dwrite_bi *dw, const std::string &text,
                                        float startColumn, float x, float y,
                                        float clipRight, const D2D1_COLOR_F &color,
                                        const D2D1_COLOR_F &bracketColor, float advance)
{
    if (text.empty() || !dw->pTextFormatPanel)
        return;

    size_t i = 0;
    while (i < text.size())
    {
        bool isBracket = (text[i] == '[' || text[i] == ']');

        size_t j = i;
        while (j < text.size() && ((text[j] == '[' || text[j] == ']') == isBracket))
            ++j;

        std::string chunk = text.substr(i, j - i);
        std::wstring run(chunk.begin(), chunk.end());

        float runX = x + (startColumn + (float)i) * advance;

        pBrush->SetColor(isBracket ? bracketColor : color);

        D2D1_RECT_F rect = D2D1::RectF(runX, y, clipRight, y + 40.0f);
        rt->DrawText(run.c_str(), (UINT32)run.size(), dw->pTextFormatPanel, rect, pBrush,
                     D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);

        i = j;
    }
}

void draw_batteryinfo_bi::drawPanelSeries(const hud_series_bi &series, double scaleMax,
                                          float left, float right, float top, float axisY,
                                          const D2D1_COLOR_F &color)
{
    size_t n = series.size();
    if (n < 2 || scaleMax <= 0.0)
        return;

    float height = axisY - top;
    float step = (right - left) / (float)(HUD_SAMPLE_COUNT - 1);

    float prevX = 0.0f;
    float prevY = 0.0f;

    for (size_t i = 0; i < n; ++i)
    {
        double v = series.at(i);
        if (v < 0.0)
            v = 0.0;

        float px = right - (float)(n - 1 - i) * step;
        float py = axisY - (float)(v / scaleMax) * height;

        if (px < left)
        {
            prevX = px;
            prevY = py;
            continue;
        }

        if (i > 0 && prevX >= left)
            line(prevX, prevY, px, py, color, 1.0f);

        prevX = px;
        prevY = py;
    }
}

D2D1_SIZE_F draw_batteryinfo_bi::measureOverlayPanel(overlay_bi *ov) const
{
    const hud_layout_bi &lay = ov->layout;

    std::vector<hud_element_bi> &elements = panelScratch;
    ov->hud.buildLayoutInto(elements);

    if (elements.empty())
        return D2D1::SizeF(200.0f, 40.0f);

    float height = lay.padTop;
    bool lastWasGraph = false;

    for (size_t i = 0; i < elements.size(); ++i)
    {
        if (elements[i].kind == HUD_EL_ROW)
        {
            height += lay.lineHeight;
            lastWasGraph = false;
        }
        else
        {
            height += lay.graphTopNudge + lay.graphHeight + lay.graphGap;
            lastWasGraph = true;
        }
    }

    if (lastWasGraph)
        height -= lay.graphGap;

    height += lay.padBottom;

    return D2D1::SizeF(lay.panelWidthFor(ov->hud.columnsFor(elements)), height);
}

void draw_batteryinfo_bi::drawOverlayPreview(init_dwrite_bi *dw, overlay_bi *ov,
                                             resource_usage_bi *ru,
                                             float l, float t, float r, float b)
{
    (void)ru;

    const hud_layout_bi &lay = ov->layout;
    const hud_theme_bi &th = ov->theme;

    if (!dw->ensurePanelFormat(lay.charAdvance, lay.lineHeight))
        return;

    std::vector<hud_element_bi> elements;
    ov->hud.buildLayoutInto(elements);

    if (elements.empty())
    {
        fillRR(l, t, l + 200.0f, t + 40.0f, lay.cornerRadius, th.panel);
        txt(dw->pTextFormatLabel, l + 12.0f, t, l + 200.0f, t + 40.0f,
            D2D1::ColorF(0x8C8C8C), L"Nothing enabled");
        return;
    }

    int columns = ov->hud.columnsFor(elements);

    D2D1_SIZE_F panel = measureOverlayPanel(ov);
    float panelW = panel.width;
    float panelH = panel.height;

    float scale = 1.0f;
    if (panelW > (r - l) && panelW > 1.0f)
        scale = (r - l) / panelW;
    if (panelH > (b - t) && panelH > 1.0f)
    {
        float vertical = (b - t) / panelH;
        if (vertical < scale)
            scale = vertical;
    }
    if (scale < 0.25f)
        scale = 0.25f;

    rt->PushAxisAlignedClip(D2D1::RectF(l, t, r, b), D2D1_ANTIALIAS_MODE_ALIASED);

    if (scale < 0.999f)
    {
        rt->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale, D2D1::Point2F(l, t)));
        previewScale = scale;
    }
    else
    {
        previewScale = 1.0f;
    }

    fillRR(l, t, l + panelW, t + panelH, lay.cornerRadius, th.panel);

    float y = t + lay.padTop;
    float left = l + lay.padLeft;
    float right = l + panelW - lay.padRight;

    for (size_t i = 0; i < elements.size(); ++i)
    {
        const hud_element_bi &el = elements[i];

        if (el.kind == HUD_EL_ROW)
        {
            const D2D1_COLOR_F &rowColor = ov->resolveColor(el.row.color);
            float textY = y - dw->panelGlyphTopOffset;

            drawPanelText(dw, el.row.left, 0.0f, left, textY, l + panelW,
                          rowColor, th.bracket, lay.charAdvance);

            if (!el.row.right.empty())
            {
                float column = (float)columns - (float)el.row.right.size();
                if (column < 0.0f)
                    column = 0.0f;

                drawPanelText(dw, el.row.right, column, left, textY, l + panelW,
                              rowColor, th.bracket, lay.charAdvance);
            }

            y += lay.lineHeight;
        }
        else
        {
            float top = y + lay.graphTopNudge;
            float axisY = top + lay.graphHeight;

            int divisions = lay.gridDivisions > 0 ? lay.gridDivisions : 1;
            float span = (right - left) - 1.0f;
            float stepX = span / (float)divisions;

            for (int d = 0; d <= divisions; ++d)
            {
                bool isEdge = (d == 0 || d == divisions);
                float gx = left + 0.5f + (float)d * stepX;
                line(gx, top, gx, axisY, isEdge ? th.axis : th.grid, lay.gridStroke);
            }

            line(left, axisY, right, axisY, th.axis, lay.axisStroke);

            double scaleMax = 0.0;
            for (int m = 0; m < HUD_M_COUNT; ++m)
            {
                const hud_metric_bi &metric = ov->hud.metrics[m];
                if (metric.graph == el.graph && metric.show && metric.graphed &&
                    metric.available && metric.series.maximum() > scaleMax)
                {
                    scaleMax = metric.series.maximum();
                }
            }

            if (scaleMax <= 0.0)
                scaleMax = 1.0;

            for (int m = 0; m < HUD_M_COUNT; ++m)
            {
                const hud_metric_bi &metric = ov->hud.metrics[m];
                if (metric.graph != el.graph || !metric.show || !metric.graphed ||
                    !metric.available)
                    continue;

                drawPanelSeries(metric.series, scaleMax, left, right, top, axisY,
                                ov->resolveColor(metric.color));
            }

            y = axisY + lay.graphGap;
        }
    }

    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    rt->PopAxisAlignedClip();
}


void draw_batteryinfo_bi::drawSettingsTab(ID2D1HwndRenderTarget *pRT, init_dwrite_bi *dw,
                                          overlay_bi *ov, resource_usage_bi *ru,
                                          batteryinfo_bi *bi)
{
    beginFrame(pRT);

    float L = contentLeft();
    float R = contentRight();
    float shift = -scrollOffsetY;

    bool overlayOn = ov->show_on_screen_display;
    bool frameOk = ov->hud.metrics[HUD_M_FPS].available;

    float y = TABBAR_H + 17.0f + shift;

    card(L, y, R, y + 62.0f);

    if (isHovered(HIT_TOGGLE, MASTER_TOGGLE_INDEX))
        hoverFill(L + 1.0f, y + 1.0f, R - 1.0f, y + 61.0f, CARD_R);

    txt(dw->pTextFormatStrong, L + CARD_PAD, y + 11.0f, L + 320.0f, y + 31.0f, pal.text,
        L"Show overlay");
    txt(dw->pTextFormatLabel, L + CARD_PAD, y + 33.0f, L + 360.0f, y + 49.0f, pal.faint,
        L"The panel drawn on top of games");

    float toggleX = R - CARD_PAD - TOGGLE_W;
    toggle(toggleX, y + 19.0f, overlayOn, false);
    pushHit(D2D1::RectF(L, y, R, y + 62.0f), HIT_TOGGLE, MASTER_TOGGLE_INDEX,
            &ov->show_on_screen_display);

    y += 62.0f + CARD_GAP;

    txt(dw->pTextFormatLabel, L, y, L + 50.0f, y + 28.0f, pal.faint, L"Preset");

    if (settingsDirty)
    {
        cachedPreset = detectPreset(ov, ru, bi);
        settingsDirty = false;
    }

    int currentPreset = cachedPreset;

    float px = L + 54.0f;
    for (int i = 0; i < 3; ++i)
    {
        float w = PRESET_WIDTHS[i];
        bool on = (currentPreset == i);
        bool hot = isHovered(HIT_PRESET, i);

        if (on)
        {
            fillRR(px, y, px + w, y + 28.0f, BTN_R, accentColor);
            txt(dw->pTextFormatLabel, px, y, px + w, y + 28.0f, D2D1::ColorF(0xFFFFFF),
                PRESET_NAMES[i], DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        else
        {
            if (hot)
                hoverFill(px, y, px + w, y + 28.0f, BTN_R);

            strokeRR(px, y, px + w, y + 28.0f, BTN_R, hot ? pal.faint : pal.border, 1.0f);
            txt(dw->pTextFormatLabel, px, y, px + w, y + 28.0f, pal.text, PRESET_NAMES[i],
                DWRITE_TEXT_ALIGNMENT_CENTER);
        }

        if (on && hot)
            hoverRing(px, y, px + w, y + 28.0f, BTN_R);

        pushHit(D2D1::RectF(px, y, px + w, y + 28.0f), HIT_PRESET, i, nullptr);
        px += w + 6.0f;
    }

    int groupTotal[GROUP_COUNT];
    int groupOn[GROUP_COUNT];
    int totalOn = 0;
    int totalRows = 0;

    for (int g = 0; g < GROUP_COUNT; ++g)
    {
        groupTotal[g] = buildGroupRows(g, ov, ru, bi, rowScratch);

        groupOn[g] = 0;
        for (size_t i = 0; i < rowScratch.size(); ++i)
        {
            if (rowScratch[i].show && *rowScratch[i].show)
                ++groupOn[g];
        }

        totalRows += groupTotal[g];
        totalOn += groupOn[g];
    }

    txt(dw->pTextFormatMicro, R - 200.0f, y, R, y + 28.0f, pal.faint,
        wfmt(L"%d of %d shown", totalOn, totalRows), DWRITE_TEXT_ALIGNMENT_TRAILING);

    y += 28.0f + CARD_GAP;

    txt(dw->pTextFormatLabel, L, y, L + 82.0f, y + 28.0f, pal.faint, L"My presets");

    std::string curLayout = captureLayout(ov, ru, bi);

    float upx = L + 88.0f;
    for (int i = 0; i < 3; ++i)
    {
        bool filled = !userPreset[i].empty();
        bool activeSlot = filled && curLayout == userPreset[i];

        float aw = 44.0f;
        float sw = 24.0f;

        if (activeSlot)
        {
            fillRR(upx, y, upx + aw, y + 28.0f, BTN_R, accentColor);
            txt(dw->pTextFormatLabel, upx, y, upx + aw, y + 28.0f, D2D1::ColorF(0xFFFFFF),
                wfmt(L"%d", i + 1), DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        else
        {
            bool hot = filled && isHovered(HIT_USERPRESET_APPLY, i);
            if (hot)
                hoverFill(upx, y, upx + aw, y + 28.0f, BTN_R);
            strokeRR(upx, y, upx + aw, y + 28.0f, BTN_R, hot ? pal.faint : pal.border, 1.0f);
            txt(dw->pTextFormatLabel, upx, y, upx + aw, y + 28.0f,
                filled ? pal.text : pal.disabled, wfmt(L"%d", i + 1),
                DWRITE_TEXT_ALIGNMENT_CENTER);
        }

        if (filled)
            pushHit(D2D1::RectF(upx, y, upx + aw, y + 28.0f), HIT_USERPRESET_APPLY, i, nullptr);

        float sx = upx + aw + 3.0f;
        bool shot = isHovered(HIT_USERPRESET_SAVE, i);
        if (shot)
            hoverFill(sx, y, sx + sw, y + 28.0f, BTN_R);
        strokeRR(sx, y, sx + sw, y + 28.0f, BTN_R, shot ? pal.faint : pal.border, 1.0f);

        float cxs = sx + sw * 0.5f;
        float cys = y + 14.0f;
        line(cxs, cys - 5.0f, cxs, cys + 4.0f, pal.text, 1.6f);
        line(cxs - 4.0f, cys, cxs, cys + 4.0f, pal.text, 1.6f);
        line(cxs + 4.0f, cys, cxs, cys + 4.0f, pal.text, 1.6f);

        pushHit(D2D1::RectF(sx, y, sx + sw, y + 28.0f), HIT_USERPRESET_SAVE, i, nullptr);

        upx = sx + sw + 12.0f;
    }

    if (R - upx > 210.0f)
        txt(dw->pTextFormatMicro, upx + 6.0f, y, R, y + 28.0f, pal.faint,
            L"number applies \u00B7 arrow saves current setup");

    y += 28.0f + CARD_GAP;

    float railTop = y;
    D2D1_COLOR_F active = accentText();

    for (int g = 0; g < GROUP_COUNT; ++g)
    {
        float ry = railTop + g * (RAIL_ITEM_H + RAIL_GAP);
        bool on = (settingsGroup == g);

        if (on)
        {
            fillRR(L, ry, L + RAIL_W, ry + RAIL_ITEM_H, BTN_R, tint(active, 0.12f));
            fillRR(L, ry + 9.0f, L + 3.0f, ry + 27.0f, 1.5f, active);
        }
        else if (isHovered(HIT_RAIL, g))
        {
            hoverFill(L, ry, L + RAIL_W, ry + RAIL_ITEM_H, BTN_R);
        }

        txt(dw->pTextFormatSmall, L + 14.0f, ry, L + 110.0f, ry + RAIL_ITEM_H,
            on ? active : pal.muted, GROUP_NAMES[g]);

        if (g == GROUP_FRAME && !frameOk)
        {
            txt(dw->pTextFormatMicro, L + 14.0f, ry, L + RAIL_W - 12.0f, ry + RAIL_ITEM_H,
                pal.warn, L"admin", DWRITE_TEXT_ALIGNMENT_TRAILING);
        }
        else
        {
            txt(dw->pTextFormatMicro, L + 14.0f, ry, L + RAIL_W - 12.0f, ry + RAIL_ITEM_H,
                pal.faint, wfmt(L"%d/%d", groupOn[g], groupTotal[g]),
                DWRITE_TEXT_ALIGNMENT_TRAILING);
        }

        pushHit(D2D1::RectF(L, ry, L + RAIL_W, ry + RAIL_ITEM_H), HIT_RAIL, g, nullptr);
    }

    float railBottom = railTop + (float)GROUP_COUNT * (RAIL_ITEM_H + RAIL_GAP);

    D2D1_SIZE_F panel = measureOverlayPanel(ov);

    float sideColumn = panel.width + CARD_PAD * 2.0f;
    if (sideColumn < 260.0f)
        sideColumn = 260.0f;

    bool sidePreview = (R - L) >= (RAIL_W + 16.0f + 320.0f + 16.0f + sideColumn);

    float paneRight = sidePreview ? (R - sideColumn - 16.0f) : R;
    float PX = L + RAIL_W + 16.0f;

    txt(dw->pTextFormatStrong, PX, y, PX + 240.0f, y + 20.0f, pal.text,
        GROUP_NAMES[settingsGroup]);
    txt(dw->pTextFormatLabel, PX, y + 23.0f, paneRight, y + 38.0f, pal.muted,
        GROUP_DESC[settingsGroup]);

    float paneY = y + 49.0f;

    bool groupDimmed = (settingsGroup != GROUP_BEHAVIOR) && !overlayOn;

    if (settingsGroup == GROUP_FRAME && !frameOk && !processElevated)
    {
        float bannerH = 76.0f;
        fillRR(PX, paneY, paneRight, paneY + bannerH, 8.0f, tint(pal.warn, 0.12f));
        strokeRR(PX, paneY, paneRight, paneY + bannerH, 8.0f, pal.warn, 1.0f);

        txt(dw->pTextFormatSmall, PX + 14.0f, paneY + 10.0f, paneRight - 14.0f, paneY + 28.0f,
            pal.warn, L"Frame timing needs administrator rights");
        txt(dw->pTextFormatLabel, PX + 14.0f, paneY + 30.0f, paneRight - 14.0f, paneY + 46.0f,
            pal.muted, L"ETW session creation is refused for standard users.");

        fillRR(PX + 14.0f, paneY + 50.0f, PX + 146.0f, paneY + 66.0f + 14.0f, BTN_R, pal.warn);
        txt(dw->pTextFormatLabel, PX + 14.0f, paneY + 50.0f, PX + 146.0f, paneY + 80.0f,
            D2D1::ColorF(0x1A1A1A), L"Restart as admin", DWRITE_TEXT_ALIGNMENT_CENTER);

        if (isHovered(HIT_ACTION, ACT_RESTART_ADMIN))
            hoverRing(PX + 14.0f, paneY + 50.0f, PX + 146.0f, paneY + 80.0f, BTN_R);

        pushHit(D2D1::RectF(PX + 14.0f, paneY + 50.0f, PX + 146.0f, paneY + 80.0f),
                HIT_ACTION, ACT_RESTART_ADMIN, nullptr);

        paneY += bannerH + CARD_GAP;
    }
    else if (settingsGroup == GROUP_FRAME && !frameOk)
    {
        float bannerH = 52.0f;
        fillRR(PX, paneY, paneRight, paneY + bannerH, 8.0f, pal.inset);

        txt(dw->pTextFormatSmall, PX + 14.0f, paneY + 9.0f, paneRight - 14.0f, paneY + 27.0f,
            pal.text, L"Frame timing is on \u2014 waiting for a full-screen app");
        txt(dw->pTextFormatLabel, PX + 14.0f, paneY + 28.0f, paneRight - 14.0f, paneY + 44.0f,
            pal.muted, L"Open a game or 3D app and its frame data appears here.");

        paneY += bannerH + CARD_GAP;
    }
    else if (groupDimmed)
    {
        fillRR(PX, paneY, paneRight, paneY + 40.0f, 8.0f, pal.inset);
        txt(dw->pTextFormatLabel, PX + 14.0f, paneY, paneRight - 14.0f, paneY + 40.0f, pal.muted,
            L"The overlay is off. These rows still save \u2014 nothing is drawn.");
        paneY += 40.0f + CARD_GAP;
    }

    if (settingsGroup == GROUP_MEMORY && ru)
    {
        float uH = 46.0f;
        card(PX, paneY, paneRight, paneY + uH);
        txt(dw->pTextFormatValue, PX + 14.0f, paneY, PX + 220.0f, paneY + uH, pal.text,
            L"Memory units");

        const wchar_t *unitLabels[3] = {L"Auto", L"MB", L"GB"};
        float segW = 56.0f;
        float segH = 26.0f;
        float segY = paneY + (uH - segH) * 0.5f;
        float segX0 = paneRight - 14.0f - 3.0f * segW - 2.0f * 6.0f;

        for (int u = 0; u < 3; ++u)
        {
            float bx = segX0 + u * (segW + 6.0f);
            bool on = (ru->memUnit == u);
            bool hot = isHovered(HIT_MEMUNIT, u);

            if (on)
            {
                fillRR(bx, segY, bx + segW, segY + segH, BTN_R, accentColor);
                txt(dw->pTextFormatMicro, bx, segY, bx + segW, segY + segH,
                    D2D1::ColorF(0xFFFFFF), unitLabels[u], DWRITE_TEXT_ALIGNMENT_CENTER);
            }
            else
            {
                if (hot)
                    hoverFill(bx, segY, bx + segW, segY + segH, BTN_R);
                strokeRR(bx, segY, bx + segW, segY + segH, BTN_R, hot ? pal.faint : pal.border, 1.0f);
                txt(dw->pTextFormatMicro, bx, segY, bx + segW, segY + segH, pal.text,
                    unitLabels[u], DWRITE_TEXT_ALIGNMENT_CENTER);
            }

            pushHit(D2D1::RectF(bx, segY, bx + segW, segY + segH), HIT_MEMUNIT, u, nullptr);
        }

        paneY += uH + CARD_GAP;
    }

    std::vector<row_bi> &rows = rowScratch;
    int rowCount = buildGroupRows(settingsGroup, ov, ru, bi, rows);

    bool useToggles = (settingsGroup == GROUP_BEHAVIOR);
    float rowHeight = useToggles ? ROW_H : METRIC_ROW_H;
    float cardH = rowCount * rowHeight + 16.0f;

    card(PX, paneY, paneRight, paneY + cardH);

    for (int i = 0; i < rowCount; ++i)
    {
        const row_bi &row = rows[i];
        float ry = paneY + 8.0f + i * rowHeight;
        bool dimmed = groupDimmed || !row.available;

        bool rowHot = row.available && (isHovered(HIT_TOGGLE, i) || isHovered(HIT_ROW_CHIP, i) ||
                                        isHovered(HIT_GRAPH_CHIP, i));

        if (rowHot)
            hoverFill(PX + 1.0f, ry + 1.0f, paneRight - 1.0f, ry + rowHeight - 1.0f, 4.0f);

        if (i > 0)
            line(PX + 14.0f, ry, paneRight - 14.0f, ry, pal.border, 1.0f);

        float labelX = PX + 14.0f;

        float descRight = paneRight - 140.0f;

        if (row.color != MC_NONE)
        {
            float dotY = row.desc ? (ry + 19.0f) : (ry + rowHeight * 0.5f);
            fillEl(PX + 18.0f, dotY, 4.0f,
                   dimmed ? pal.disabled : metricColor(row.color));
            labelX = PX + 28.0f;
        }

        if (row.desc)
        {
            txt(dw->pTextFormatValue, labelX, ry + 8.0f, descRight, ry + 30.0f,
                dimmed ? pal.disabled : pal.text, row.label, DWRITE_TEXT_ALIGNMENT_LEADING, true);
            txt(dw->pTextFormatMicro, labelX, ry + 30.0f, descRight, ry + 46.0f,
                dimmed ? pal.disabled : pal.faint, row.desc, DWRITE_TEXT_ALIGNMENT_LEADING, true);
        }
        else
        {
            txt(dw->pTextFormatValue, labelX, ry, descRight, ry + rowHeight,
                dimmed ? pal.disabled : pal.text, row.label);
        }

        if (useToggles)
        {
            float tx = paneRight - 14.0f - TOGGLE_W;
            toggle(tx, ry + (rowHeight - TOGGLE_H) * 0.5f, *row.show, dimmed);
            if (row.available)
                pushHit(D2D1::RectF(PX, ry, paneRight, ry + rowHeight), HIT_TOGGLE, i, row.show);
        }
        else
        {
            float chipY = ry + (rowHeight - CHIP_H) * 0.5f;
            float rowChipX = paneRight - 14.0f - CHIP_ROW_W;

            chip(dw, rowChipX, chipY, CHIP_ROW_W, L"Row", *row.show, dimmed);
            if (row.available)
            {
                if (isHovered(HIT_ROW_CHIP, i))
                    hoverRing(rowChipX, chipY, rowChipX + CHIP_ROW_W, chipY + CHIP_H, CHIP_R);
                pushHit(D2D1::RectF(rowChipX, chipY, rowChipX + CHIP_ROW_W, chipY + CHIP_H),
                        HIT_ROW_CHIP, i, row.show);
            }

            if (row.graph)
            {
                float graphChipX = rowChipX - 6.0f - CHIP_GRAPH_W;
                chip(dw, graphChipX, chipY, CHIP_GRAPH_W, L"Graph", *row.graph, dimmed);
                if (row.available)
                {
                    if (isHovered(HIT_GRAPH_CHIP, i))
                        hoverRing(graphChipX, chipY, graphChipX + CHIP_GRAPH_W, chipY + CHIP_H, CHIP_R);
                    pushHit(D2D1::RectF(graphChipX, chipY, graphChipX + CHIP_GRAPH_W, chipY + CHIP_H),
                            HIT_GRAPH_CHIP, i, row.graph);
                }
            }
        }
    }

    float paneBottom = paneY + cardH;

    if (settingsGroup == GROUP_CPU && !ru->cpuInfo.packagePowerAvailable)
    {
        txt(dw->pTextFormatMicro, PX, paneBottom + 8.0f, paneRight, paneBottom + 23.0f, pal.faint,
            L"Package power counter is not present on this machine.");
        paneBottom += 23.0f;
    }

    float bottom = (paneBottom > railBottom) ? paneBottom : railBottom;

    float previewLeft = sidePreview ? (R - sideColumn) : L;
    float previewRight = R;
    float previewTop = sidePreview ? railTop : (bottom + CARD_GAP + 8.0f);

    float boxW = previewRight - previewLeft - CARD_PAD * 2.0f;

    float availableH = sidePreview
                           ? (viewHeight - previewTop - PAD - 52.0f)
                           : (viewHeight - TABBAR_H - 120.0f);
    if (availableH < 140.0f)
        availableH = 140.0f;

    float fit = 1.0f;
    if (panel.width > boxW && panel.width > 1.0f)
        fit = boxW / panel.width;
    if (panel.height > availableH && panel.height > 1.0f)
    {
        float vertical = availableH / panel.height;
        if (vertical < fit)
            fit = vertical;
    }
    if (fit < 0.25f)
        fit = 0.25f;

    float previewH = panel.height * fit + 52.0f;
    if (previewH < 120.0f)
        previewH = 120.0f;

    card(previewLeft, previewTop, previewRight, previewTop + previewH);

    eyebrow(dw, previewLeft + CARD_PAD, previewTop + 16.0f,
            fit < 0.999f ? wfmt(L"PREVIEW \u00B7 %.0f%% OF ACTUAL SIZE", fit * 100.0)
                         : std::wstring(L"PREVIEW \u00B7 ACTUAL SIZE"));

    drawOverlayPreview(dw, ov, ru, previewLeft + CARD_PAD, previewTop + 36.0f,
                       previewRight - CARD_PAD, previewTop + previewH - 16.0f);

    float previewBottom = previewTop + previewH;
    y = (previewBottom > bottom ? previewBottom : bottom) + PAD;

    contentHeight = y + scrollOffsetY;
    clampScroll();
    drawScrollbar();
}

float draw_batteryinfo_bi::drawRunCard(init_dwrite_bi *dw, float L, float R, float y,
                                       const capture_bi::summary_bi &s, const wchar_t *title)
{
    float IX = L + CARD_PAD;
    float IR = R - CARD_PAD;
    float IW = IR - IX;

    card(L, y, R, y + 232.0f);
    eyebrow(dw, IX, y + 16.0f, title);

    txt(dw->pTextFormatSmall, IR - 320.0f, y + 14.0f, IR, y + 30.0f, pal.muted,
        widen(s.process) + wfmt(L" \u00B7 %.0f s \u00B7 %u frames", s.seconds, (unsigned)s.frames),
        DWRITE_TEXT_ALIGNMENT_TRAILING, true);

    float colW = (IW - 32.0f) / 3.0f;
    float c1 = IX;
    float c2 = IX + colW + 16.0f;
    float c3 = IX + (colW + 16.0f) * 2.0f;
    float rowY = y + 44.0f;

    statCell(dw, c1, rowY, colW, L"AVERAGE", wfmt(L"%.1f fps", s.avgFps), false);
    statCell(dw, c2, rowY, colW, L"1% LOW",
             s.low1Valid ? wfmt(L"%.1f fps", s.low1Fps) : L"\u2014", !s.low1Valid);
    statCell(dw, c3, rowY, colW, L"0.1% LOW",
             s.low01Valid ? wfmt(L"%.1f fps", s.low01Fps) : L"\u2014", !s.low01Valid);

    rowY += 52.0f;

    statCell(dw, c1, rowY, colW, L"MEDIAN FRAME", wfmt(L"%.2f ms", s.medianMs), false);
    statCell(dw, c2, rowY, colW, L"WORST FRAME", wfmt(L"%.1f ms", s.maxMs), false);
    statCell(dw, c3, rowY, colW, L"STUTTERS", wfmt(L"%u", s.stutters), false);

    rowY += 52.0f;

    statCell(dw, c1, rowY, colW, L"CPU PACKAGE",
             s.packageValid ? wfmt(L"%.1f W", s.avgPackageW) : L"\u2014", !s.packageValid);
    statCell(dw, c2, rowY, colW, L"ENERGY USED",
             s.energyValid ? wfmt(L"%.2f Wh", s.energyWh) : L"\u2014", !s.energyValid);
    statCell(dw, c3, rowY, colW, L"BATTERY LIFE",
             s.projectionValid ? wfmt(L"%.1f h", s.projectedHours) : L"\u2014",
             !s.projectionValid);

    rowY += 46.0f;

    bool effOk = s.energyValid && s.energyWh > 0.0 && s.frames > 0;

    if (effOk)
    {
        txt(dw->pTextFormatMicro, IX, rowY, IR, rowY + 15.0f, pal.faint,
            wfmt(L"%.0f frames per watt-hour", (double)s.frames / s.energyWh));
    }
    else
    {
        txt(dw->pTextFormatMicro, IX, rowY, IR, rowY + 15.0f, pal.faint,
            L"Package power was not readable, so energy could not be measured.");
    }

    return 232.0f;
}

void draw_batteryinfo_bi::drawCaptureTab(ID2D1HwndRenderTarget *pRT, init_dwrite_bi *dw,
                                         const capture_view_bi &cap)
{
    beginFrame(pRT);

    float L = contentLeft();
    float R = contentRight();
    float IX = L + CARD_PAD;
    float IR = R - CARD_PAD;
    float shift = -scrollOffsetY;

    float y = TABBAR_H + PAD + shift;

    card(L, y, R, y + 128.0f);
    eyebrow(dw, IX, y + 16.0f, L"SESSION");

    if (cap.recording)
    {
        int mins = (int)(cap.seconds / 60.0);
        int secs = (int)cap.seconds % 60;

        fillEl(IX + 5.0f, y + 47.0f, 5.0f, pal.bad);
        txt(dw->pTextFormatDisplay, IX + 18.0f, y + 26.0f, IX + 200.0f, y + 68.0f, pal.text,
            wfmt(L"%d:%02d", mins, secs));

        txt(dw->pTextFormatSmall, IR - 260.0f, y + 32.0f, IR, y + 50.0f, pal.muted,
            wfmt(L"%u frames", (unsigned)cap.frames), DWRITE_TEXT_ALIGNMENT_TRAILING);
        txt(dw->pTextFormatSmall, IR - 260.0f, y + 50.0f, IR, y + 68.0f, pal.muted,
            cap.liveLow1Valid
                ? wfmt(L"%.0f fps avg \u00B7 %.0f low", cap.liveFps, cap.liveLow1)
                : wfmt(L"%.0f fps avg", cap.liveFps),
            DWRITE_TEXT_ALIGNMENT_TRAILING);

        button(dw, IX, y + 80.0f, 160.0f, L"Stop and save", true,
               isHovered(HIT_ACTION, ACT_TOGGLE_CAPTURE));
    }
    else
    {
        txt(dw->pTextFormatStrong, IX, y + 32.0f, IR, y + 52.0f, pal.text,
            L"Record a run and measure it");
        txt(dw->pTextFormatSmall, IX, y + 54.0f, IR, y + 72.0f, pal.muted,
            L"Frame times, package power and what the run cost the battery.");

        button(dw, IX, y + 80.0f, 160.0f, L"Start recording", true,
               isHovered(HIT_ACTION, ACT_TOGGLE_CAPTURE));
    }

    pushHit(D2D1::RectF(IX, y + 80.0f, IX + 160.0f, y + 80.0f + BTN_H),
            HIT_ACTION, ACT_TOGGLE_CAPTURE, nullptr);

    txt(dw->pTextFormatMicro, IX + 172.0f, y + 80.0f, IR, y + 112.0f, pal.faint,
        L"or press Ctrl+Alt+B anywhere");

    y += 128.0f + CARD_GAP;

    if (cap.hasLast)
        y += drawRunCard(dw, L, R, y, cap.last, L"LAST RUN") + CARD_GAP;

    float rowPitch = 40.0f;
    size_t historyRows = cap.history.size();

    bool expandInline = (selectedRun >= 0 && (size_t)selectedRun < historyRows);

    float historyH = 66.0f + (float)historyRows * rowPitch;
    if (expandInline)
        historyH += 232.0f + CARD_GAP;
    if (cap.history.empty())
        historyH = 64.0f;

    card(L, y, R, y + historyH);
    eyebrow(dw, IX, y + 16.0f, L"SAVED RUNS");

    if (cap.history.empty())
    {
        txt(dw->pTextFormatSmall, IX, y + 34.0f, IR, y + 52.0f, pal.faint,
            L"Nothing recorded yet.");
    }
    else
    {
        txt(dw->pTextFormatMicro, IR - 220.0f, y + 18.0f, IR, y + 32.0f, pal.faint,
            L"Click a run to see its full stats", DWRITE_TEXT_ALIGNMENT_TRAILING);

        float listY = y + 40.0f;

        for (size_t i = 0; i < historyRows; ++i)
        {
            const capture_bi::summary_bi &h = cap.history[i];
            float ry = listY;

            bool sel = ((int)i == selectedRun);
            bool hot = isHovered(HIT_CAPTURE_RUN, (int)i);

            if (sel)
                fillRR(IX - 4.0f, ry + 2.0f, IR + 4.0f, ry + rowPitch - 2.0f, 6.0f,
                       tint(accentColor, 0.10f));
            else if (hot)
                hoverFill(IX - 4.0f, ry + 2.0f, IR + 4.0f, ry + rowPitch - 2.0f, 6.0f);

            if (i > 0 && !sel)
                line(IX, ry, IR, ry, pal.border, 1.0f);

            fillEl(IX + 4.0f, ry + rowPitch * 0.5f, 3.0f,
                   sel ? accentText() : pal.disabled);

            txt(dw->pTextFormatValue, IX + 18.0f, ry + 5.0f, IX + 210.0f, ry + 23.0f,
                pal.text, widen(h.process), DWRITE_TEXT_ALIGNMENT_LEADING, true);
            txt(dw->pTextFormatMicro, IX + 18.0f, ry + 22.0f, IX + 260.0f, ry + 37.0f,
                pal.faint, widen(h.label), DWRITE_TEXT_ALIGNMENT_LEADING, true);

            txt(dw->pTextFormatValue, IR - 210.0f, ry + 5.0f, IR - 22.0f, ry + 23.0f,
                pal.text, wfmt(L"%.0f fps", h.avgFps), DWRITE_TEXT_ALIGNMENT_TRAILING);
            txt(dw->pTextFormatMicro, IR - 210.0f, ry + 22.0f, IR - 22.0f, ry + 37.0f,
                pal.muted,
                h.energyValid ? wfmt(L"%.0f s \u00B7 %.2f Wh", h.seconds, h.energyWh)
                              : wfmt(L"%.0f s", h.seconds),
                DWRITE_TEXT_ALIGNMENT_TRAILING);

            float chev = IR - 12.0f;
            float chevY = ry + rowPitch * 0.5f;
            const D2D1_COLOR_F &chevC = sel ? accentText() : pal.faint;
            if (sel)
            {
                line(chev - 5.0f, chevY - 2.0f, chev, chevY + 3.0f, chevC, 1.6f);
                line(chev, chevY + 3.0f, chev + 5.0f, chevY - 2.0f, chevC, 1.6f);
            }
            else
            {
                line(chev - 2.0f, chevY - 5.0f, chev + 3.0f, chevY, chevC, 1.6f);
                line(chev + 3.0f, chevY, chev - 2.0f, chevY + 5.0f, chevC, 1.6f);
            }

            pushHit(D2D1::RectF(IX - 4.0f, ry, IR + 4.0f, ry + rowPitch),
                    HIT_CAPTURE_RUN, (int)i, nullptr);

            listY += rowPitch;

            if (sel)
                listY += drawRunCard(dw, IX - CARD_PAD + 10.0f, IR + CARD_PAD - 10.0f,
                                     listY, h, L"RUN DETAIL") +
                         CARD_GAP;
        }
    }

    y += historyH + CARD_GAP;

    button(dw, L, y, 150.0f, L"Open captures folder", false,
           isHovered(HIT_ACTION, ACT_OPEN_CAPTURES));
    pushHit(D2D1::RectF(L, y, L + 150.0f, y + BTN_H), HIT_ACTION, ACT_OPEN_CAPTURES, nullptr);

    y += BTN_H + PAD;

    contentHeight = y + scrollOffsetY + FOOTER_H;
    clampScroll();

    drawFooter(dw, L"Frame times are written as CSV, one row per frame.");
    drawScrollbar();
}

void draw_batteryinfo_bi::drawAppearanceTab(ID2D1HwndRenderTarget *pRT, init_dwrite_bi *dw,
                                            overlay_bi *ov)
{
    beginFrame(pRT);

    float L = contentLeft();
    float R = contentRight();
    float IX = L + CARD_PAD;
    float IR = R - CARD_PAD;
    float IW = IR - IX;
    float shift = -scrollOffsetY;

    D2D1_COLOR_F active = accentText();
    float y = TABBAR_H + PAD + shift;

    card(L, y, R, y + 150.0f);
    eyebrow(dw, IX, y + 16.0f, L"THEME");

    float previewW = (IW - 16.0f) * 0.5f;
    float previewY = y + 38.0f;

    for (int i = 0; i < 2; ++i)
    {
        bool dark = (i == 1);
        float bx = IX + i * (previewW + 16.0f);

        D2D1_COLOR_F mbg = dark ? D2D1::ColorF(0x0F0F12) : D2D1::ColorF(0xFAFAFA);
        D2D1_COLOR_F msurface = dark ? D2D1::ColorF(0x17171C) : D2D1::ColorF(0xFFFFFF);
        D2D1_COLOR_F mborder = dark ? D2D1::ColorF(0x2E2E36) : D2D1::ColorF(0xE4E4E8);
        D2D1_COLOR_F mtext = dark ? D2D1::ColorF(0xEBEBEB) : D2D1::ColorF(0x1A1A1A);
        D2D1_COLOR_F mmuted = dark ? D2D1::ColorF(0x5C5C66) : D2D1::ColorF(0xC8C8D0);

        fillRR(bx, previewY, bx + previewW, previewY + 88.0f, 8.0f, mbg);
        strokeRR(bx, previewY, bx + previewW, previewY + 88.0f, 8.0f, mborder, 1.0f);

        fillRR(bx + 12.0f, previewY + 10.0f, bx + 22.0f, previewY + 20.0f, 3.0f, mtext);
        fillRR(bx + 28.0f, previewY + 13.0f, bx + 62.0f, previewY + 18.0f, 2.5f, mtext);
        line(bx + 1.0f, previewY + 28.0f, bx + previewW - 1.0f, previewY + 28.0f, mborder, 1.0f);

        fillRR(bx + 12.0f, previewY + 35.0f, bx + 34.0f, previewY + 40.0f, 2.5f, accentColor);
        fillRR(bx + 42.0f, previewY + 35.0f, bx + 68.0f, previewY + 40.0f, 2.5f, mmuted);
        fillRR(bx + 76.0f, previewY + 35.0f, bx + 96.0f, previewY + 40.0f, 2.5f, mmuted);

        fillRR(bx + 12.0f, previewY + 50.0f, bx + previewW - 12.0f, previewY + 78.0f,
               5.0f, msurface);
        strokeRR(bx + 12.0f, previewY + 50.0f, bx + previewW - 12.0f, previewY + 78.0f,
                 5.0f, mborder, 1.0f);
        fillRR(bx + 22.0f, previewY + 58.0f, bx + 62.0f, previewY + 63.0f, 2.5f, mmuted);
        fillRR(bx + 22.0f, previewY + 68.0f, bx + 22.0f + (previewW - 64.0f) * 0.6f,
               previewY + 74.0f, 3.0f, accentColor);

        bool selected = (dark == nightMode);
        if (selected)
            strokeRR(bx - 3.0f, previewY - 3.0f, bx + previewW + 3.0f, previewY + 91.0f,
                     10.0f, active, 2.0f);
        else if (isHovered(HIT_THEME, i))
            strokeRR(bx - 3.0f, previewY - 3.0f, bx + previewW + 3.0f, previewY + 91.0f,
                     10.0f, pal.faint, 1.5f);

        txt(dw->pTextFormatLabel, bx, previewY + 94.0f, bx + previewW, previewY + 110.0f,
            selected ? active : pal.muted, dark ? L"Dark" : L"Light",
            DWRITE_TEXT_ALIGNMENT_CENTER);

        pushHit(D2D1::RectF(bx, previewY, bx + previewW, previewY + 110.0f), HIT_THEME, i, nullptr);
    }

    y += 150.0f + CARD_GAP;

    card(L, y, R, y + 146.0f);
    eyebrow(dw, IX, y + 16.0f, L"ACCENT");

    float swatchY = y + 39.0f;
    for (size_t i = 0; i < colorPalette.size(); ++i)
    {
        float sx = IX + (i % 8) * (SWATCH + SWATCH_GAP);
        float sy = swatchY + (i / 8) * (SWATCH + SWATCH_GAP);

        fillRR(sx, sy, sx + SWATCH, sy + SWATCH, BTN_R, colorPalette[i]);

        const float eps = 0.01f;
        if (std::fabs(colorPalette[i].r - accentColor.r) < eps &&
            std::fabs(colorPalette[i].g - accentColor.g) < eps &&
            std::fabs(colorPalette[i].b - accentColor.b) < eps)
        {
            strokeRR(sx - 4.0f, sy - 4.0f, sx + SWATCH + 4.0f, sy + SWATCH + 4.0f,
                     8.0f, pal.text, 2.0f);
        }
        else if (isHovered(HIT_ACCENT, (int)i))
        {
            strokeRR(sx - 4.0f, sy - 4.0f, sx + SWATCH + 4.0f, sy + SWATCH + 4.0f,
                     8.0f, pal.faint, 1.5f);
        }

        pushHit(D2D1::RectF(sx, sy, sx + SWATCH, sy + SWATCH), HIT_ACCENT, (int)i, nullptr);
    }

    txt(dw->pTextFormatMicro, IX, y + 127.0f, IR, y + 142.0f, pal.faint,
        L"Used for highlights, the charge bar and selected controls.");

    y += 146.0f + CARD_GAP;

    card(L, y, R, y + 145.0f);
    eyebrow(dw, IX, y + 16.0f, L"OVERLAY POSITION");

    float boxY = y + 39.0f;
    float boxW = 108.0f;
    float boxH = 68.0f;
    float boxGap = 16.0f;

    for (int i = 0; i < 4; ++i)
    {
        float bx = IX + i * (boxW + boxGap);
        bool selected = (ov && (int)ov->corner == i);
        bool hot = isHovered(HIT_CORNER, i);

        if (selected)
            fillRR(bx, boxY, bx + boxW, boxY + boxH, BTN_R, tint(active, 0.10f));
        else if (hot)
            hoverFill(bx, boxY, bx + boxW, boxY + boxH, BTN_R);

        strokeRR(bx, boxY, bx + boxW, boxY + boxH, BTN_R,
                 selected ? active : (hot ? pal.faint : pal.border), selected ? 2.0f : 1.0f);

        float panelW = 40.0f;
        float panelH = 22.0f;
        float ppx = (i % 2 == 0) ? (bx + 8.0f) : (bx + boxW - 8.0f - panelW);
        float ppy = (i < 2) ? (boxY + 8.0f) : (boxY + boxH - 8.0f - panelH);

        fillRR(ppx, ppy, ppx + panelW, ppy + panelH, 4.0f, selected ? active : pal.disabled);

        txt(dw->pTextFormatMicro, bx, boxY + boxH + 6.0f, bx + boxW, boxY + boxH + 20.0f,
            selected ? active : pal.faint, CORNER_NAMES[i], DWRITE_TEXT_ALIGNMENT_CENTER);

        pushHit(D2D1::RectF(bx, boxY, bx + boxW, boxY + boxH + 20.0f), HIT_CORNER, i, nullptr);
    }

    y += 145.0f + CARD_GAP;

    card(L, y, R, y + 188.0f);
    txt(dw->pTextFormatValue, IX, y + 16.0f, IX + 300.0f, y + 34.0f, pal.text,
        L"Refresh rate");
    txt(dw->pTextFormatMicro, IX, y + 36.0f, IX + 340.0f, y + 51.0f, pal.faint,
        L"Slower redraws cost less while playing");

    const int RATES[4] = {50, 100, 250, 500};
    float rateX = IR - 4.0f * 52.0f - 3.0f * 6.0f;

    for (int i = 0; i < 4; ++i)
    {
        float bx = rateX + i * 58.0f;
        bool on = (ov && ov->refreshMs == RATES[i]);
        bool hot = isHovered(HIT_REFRESH, i);

        if (on)
        {
            fillRR(bx, y + 21.0f, bx + 52.0f, y + 51.0f, BTN_R, accentColor);
            txt(dw->pTextFormatMicro, bx, y + 21.0f, bx + 52.0f, y + 51.0f,
                D2D1::ColorF(0xFFFFFF), wfmt(L"%d ms", RATES[i]),
                DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        else
        {
            if (hot)
                hoverFill(bx, y + 21.0f, bx + 52.0f, y + 51.0f, BTN_R);

            strokeRR(bx, y + 21.0f, bx + 52.0f, y + 51.0f, BTN_R,
                     hot ? pal.faint : pal.border, 1.0f);
            txt(dw->pTextFormatMicro, bx, y + 21.0f, bx + 52.0f, y + 51.0f, pal.text,
                wfmt(L"%d ms", RATES[i]), DWRITE_TEXT_ALIGNMENT_CENTER);
        }

        pushHit(D2D1::RectF(bx, y + 21.0f, bx + 52.0f, y + 51.0f), HIT_REFRESH,
                RATES[i], nullptr);
    }

    line(IX, y + 66.0f, IR, y + 66.0f, pal.border, 1.0f);

    y += 58.0f;

    txt(dw->pTextFormatValue, IX, y + 16.0f, IX + 300.0f, y + 34.0f, pal.text,
        L"Overlay scale");
    txt(dw->pTextFormatMicro, IX, y + 36.0f, IX + 340.0f, y + 51.0f, pal.faint,
        L"Size of the in-game panel and its text");

    float scStepY = y + 21.0f;
    float scMinusX = IR - 108.0f;
    float scPlusX = IR - 30.0f;

    if (isHovered(HIT_SCALE, -1))
        hoverFill(scMinusX, scStepY, scMinusX + 30.0f, scStepY + 30.0f, BTN_R);

    strokeRR(scMinusX, scStepY, scMinusX + 30.0f, scStepY + 30.0f, BTN_R, pal.border, 1.0f);
    line(scMinusX + 9.0f, scStepY + 15.0f, scMinusX + 21.0f, scStepY + 15.0f, pal.text, 1.6f);
    pushHit(D2D1::RectF(scMinusX, scStepY, scMinusX + 30.0f, scStepY + 30.0f), HIT_SCALE, -1, nullptr);

    txt(dw->pTextFormatSmall, IR - 74.0f, scStepY, IR - 32.0f, scStepY + 30.0f, pal.text,
        wfmt(L"%d%%", ov ? ov->getScale() : 100), DWRITE_TEXT_ALIGNMENT_CENTER);

    if (isHovered(HIT_SCALE, 1))
        hoverFill(scPlusX, scStepY, scPlusX + 30.0f, scStepY + 30.0f, BTN_R);

    strokeRR(scPlusX, scStepY, scPlusX + 30.0f, scStepY + 30.0f, BTN_R, pal.border, 1.0f);
    line(scPlusX + 9.0f, scStepY + 15.0f, scPlusX + 21.0f, scStepY + 15.0f, pal.text, 1.6f);
    line(scPlusX + 15.0f, scStepY + 9.0f, scPlusX + 15.0f, scStepY + 21.0f, pal.text, 1.6f);
    pushHit(D2D1::RectF(scPlusX, scStepY, scPlusX + 30.0f, scStepY + 30.0f), HIT_SCALE, 1, nullptr);

    line(IX, y + 66.0f, IR, y + 66.0f, pal.border, 1.0f);

    y += 58.0f;

    txt(dw->pTextFormatValue, IX, y + 16.0f, IX + 300.0f, y + 34.0f, pal.text,
        L"Overlay margin");
    txt(dw->pTextFormatMicro, IX, y + 36.0f, IX + 320.0f, y + 51.0f, pal.faint,
        L"Distance from the screen edge");

    float stepY = y + 21.0f;
    float minusX = IR - 108.0f;
    float plusX = IR - 30.0f;

    if (isHovered(HIT_MARGIN, -1))
        hoverFill(minusX, stepY, minusX + 30.0f, stepY + 30.0f, BTN_R);

    strokeRR(minusX, stepY, minusX + 30.0f, stepY + 30.0f, BTN_R, pal.border, 1.0f);
    line(minusX + 9.0f, stepY + 15.0f, minusX + 21.0f, stepY + 15.0f, pal.text, 1.6f);
    pushHit(D2D1::RectF(minusX, stepY, minusX + 30.0f, stepY + 30.0f), HIT_MARGIN, -1, nullptr);

    txt(dw->pTextFormatSmall, IR - 74.0f, stepY, IR - 32.0f, stepY + 30.0f, pal.text,
        wfmt(L"%d px", ov ? ov->margin : 0), DWRITE_TEXT_ALIGNMENT_CENTER);

    if (isHovered(HIT_MARGIN, 1))
        hoverFill(plusX, stepY, plusX + 30.0f, stepY + 30.0f, BTN_R);

    strokeRR(plusX, stepY, plusX + 30.0f, stepY + 30.0f, BTN_R, pal.border, 1.0f);
    line(plusX + 9.0f, stepY + 15.0f, plusX + 21.0f, stepY + 15.0f, pal.text, 1.6f);
    line(plusX + 15.0f, stepY + 9.0f, plusX + 15.0f, stepY + 21.0f, pal.text, 1.6f);
    pushHit(D2D1::RectF(plusX, stepY, plusX + 30.0f, stepY + 30.0f), HIT_MARGIN, 1, nullptr);

    y += 72.0f + PAD;

    contentHeight = y + scrollOffsetY;
    clampScroll();
    drawScrollbar();
}

void draw_batteryinfo_bi::drawAboutTab(ID2D1HwndRenderTarget *pRT, init_dwrite_bi *dw,
                                       const diag_bi &diag, const update_view_bi &upd)
{
    beginFrame(pRT);

    float L = contentLeft();
    float R = contentRight();
    float IX = L + CARD_PAD;
    float IR = R - CARD_PAD;
    float shift = -scrollOffsetY;

    float y = TABBAR_H + PAD + shift;

    card(L, y, R, y + 176.0f);
    logoMark(IX, y + 20.0f, 48.0f);

    txt(dw->pTextFormatTitle, IX + 64.0f, y + 21.0f, IX + 320.0f, y + 53.0f, pal.text, L"Kestrel");
    txt(dw->pTextFormatLabel, IX + 64.0f, y + 54.0f, IX + 360.0f, y + 70.0f, pal.muted,
        wfmt(L"Version %d.%d.%d \u00B7 portable build",
             APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_PATCH));

    txt(dw->pTextFormatSmall, IX, y + 89.0f, IR, y + 107.0f, pal.muted,
        L"Battery health, CPU and GPU load, package power and real");
    txt(dw->pTextFormatSmall, IX, y + 107.0f, IR, y + 125.0f, pal.muted,
        L"per-application frame timing, in an always-on-top overlay.");

    button(dw, IX, y + 137.0f, 96.0f, L"Repository", false,
           isHovered(HIT_ACTION, ACT_OPEN_REPO));
    pushHit(D2D1::RectF(IX, y + 137.0f, IX + 96.0f, y + 137.0f + BTN_H),
            HIT_ACTION, ACT_OPEN_REPO, nullptr);

    button(dw, IX + 106.0f, y + 137.0f, 116.0f, L"Report an issue", false,
           isHovered(HIT_ACTION, ACT_OPEN_ISSUES));
    pushHit(D2D1::RectF(IX + 106.0f, y + 137.0f, IX + 222.0f, y + 137.0f + BTN_H),
            HIT_ACTION, ACT_OPEN_ISSUES, nullptr);

    button(dw, IX + 232.0f, y + 137.0f, 74.0f, L"Licence", false,
           isHovered(HIT_ACTION, ACT_OPEN_LICENCE));
    pushHit(D2D1::RectF(IX + 232.0f, y + 137.0f, IX + 306.0f, y + 137.0f + BTN_H),
            HIT_ACTION, ACT_OPEN_LICENCE, nullptr);

    y += 176.0f + CARD_GAP;

    float updateH = 116.0f;
    card(L, y, R, y + updateH);
    eyebrow(dw, IX, y + 16.0f, L"UPDATES");

    std::wstring status;
    D2D1_COLOR_F statusColor = pal.muted;

    std::wstring primaryLabel = L"Check for updates";
    int primaryAction = ACT_CHECK_UPDATE;
    bool primaryEnabled = true;

    switch (upd.state)
    {
    case 1:
        status = L"Contacting GitHub\u2026";
        primaryLabel = L"Checking\u2026";
        primaryEnabled = false;
        break;

    case 2:
        status = wfmt(L"%d.%d.%d is the newest release",
                      APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_PATCH);
        statusColor = pal.ok;
        break;

    case 3:
        status = L"Version " + widen(upd.version) + L" is available";
        statusColor = pal.warn;
        primaryLabel = L"Download " + widen(upd.version);
        primaryAction = ACT_DOWNLOAD_UPDATE;
        break;

    case 4:
        status = wfmt(L"Downloading\u2026 %d%%", upd.progress);
        primaryLabel = L"Downloading\u2026";
        primaryEnabled = false;
        break;

    case 5:
        status = L"Version " + widen(upd.version) + L" is ready to install";
        statusColor = pal.ok;
        primaryLabel = L"Install and restart";
        primaryAction = ACT_INSTALL_UPDATE;
        break;

    case 6:
        status = widen(upd.message);
        statusColor = pal.bad;
        primaryLabel = L"Try again";
        break;

    default:
        status = L"Kestrel does not check for updates on its own.";
        break;
    }

    txt(dw->pTextFormatSmall, IX, y + 34.0f, IR, y + 52.0f, statusColor, status);

    if (upd.state == 4)
    {
        float fraction = (float)upd.progress / 100.0f;
        bar(IX, y + 58.0f, IR - IX, 6.0f, fraction, accentColor);
    }

    float btnY = y + 74.0f;
    float primaryW = 168.0f;

    if (primaryEnabled)
    {
        button(dw, IX, btnY, primaryW, primaryLabel, true,
               isHovered(HIT_ACTION, primaryAction));
        pushHit(D2D1::RectF(IX, btnY, IX + primaryW, btnY + BTN_H),
                HIT_ACTION, primaryAction, nullptr);
    }
    else
    {
        fillRR(IX, btnY, IX + primaryW, btnY + BTN_H, BTN_R, pal.inset);
        txt(dw->pTextFormatSmall, IX, btnY, IX + primaryW, btnY + BTN_H, pal.disabled,
            primaryLabel, DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    if (upd.backupAvailable)
    {
        float rollX = IX + primaryW + 10.0f;
        float rollW = 158.0f;

        std::wstring rollLabel = upd.backupVersion.empty()
                                     ? L"Roll back"
                                     : (L"Roll back to " + widen(upd.backupVersion));

        button(dw, rollX, btnY, rollW, rollLabel, false, isHovered(HIT_ACTION, ACT_ROLLBACK));
        pushHit(D2D1::RectF(rollX, btnY, rollX + rollW, btnY + BTN_H),
                HIT_ACTION, ACT_ROLLBACK, nullptr);
    }

    y += updateH + CARD_GAP;

    float diagH = 295.0f;
    card(L, y, R, y + diagH);
    eyebrow(dw, IX, y + 16.0f, L"WHAT KESTREL CAN READ ON THIS MACHINE");

    struct diag_row
    {
        std::wstring label;
        std::wstring status;
        bool ok;
    };

    std::vector<diag_row> lines;

    diag_row d0 = {L"Frame timing (ETW)",
                   diag.frameTiming ? L"Active" : widen(diag.frameReason),
                   diag.frameTiming};
    if (d0.status.empty())
        d0.status = L"Unavailable";
    lines.push_back(d0);

    diag_row d1 = {L"CPU package power",
                   diag.cpuPower ? L"Energy Meter counter" : L"Counter not present",
                   diag.cpuPower};
    lines.push_back(d1);

    diag_row d2 = {L"GPU load",
                   diag.gpuName.empty() ? L"No adapter" : widen(diag.gpuName),
                   !diag.gpuName.empty()};
    lines.push_back(d2);

    diag_row d3 = {L"Battery",
                   diag.battery ? (L"1 device \u00B7 " + widen(diag.chemistry)) : L"No device",
                   diag.battery};
    lines.push_back(d3);

    diag_row d4 = {L"Per-core load", wfmt(L"%d threads", diag.threads), diag.threads > 0};
    lines.push_back(d4);

    for (size_t i = 0; i < lines.size(); ++i)
    {
        float ry = y + 37.0f + i * 40.0f;

        if (i > 0)
            line(IX, ry, IR, ry, pal.border, 1.0f);

        fillEl(IX + 4.0f, ry + 20.0f, 4.0f, lines[i].ok ? pal.ok : pal.warn);
        txt(dw->pTextFormatValue, IX + 20.0f, ry + 11.0f, IX + 240.0f, ry + 29.0f,
            pal.text, lines[i].label);
        txt(dw->pTextFormatLabel, IR - 260.0f, ry + 11.0f, IR, ry + 29.0f,
            lines[i].ok ? pal.muted : pal.warn, lines[i].status,
            DWRITE_TEXT_ALIGNMENT_TRAILING);
    }

    float logY = y + diagH - 46.0f;
    button(dw, IX, logY - 1.0f, 110.0f, L"Open log file", false,
           isHovered(HIT_ACTION, ACT_OPEN_LOG));
    pushHit(D2D1::RectF(IX, logY - 1.0f, IX + 110.0f, logY + 31.0f),
            HIT_ACTION, ACT_OPEN_LOG, nullptr);

    y += diagH + PAD;

    contentHeight = y + scrollOffsetY + FOOTER_H;
    clampScroll();

    drawFooter(dw, L"MIT licence \u00B7 github.com/semiloker/Kestrel");
    drawScrollbar();
}

draw_batteryinfo_bi::click_result_bi draw_batteryinfo_bi::handleClick(POINT cursorPos,
                                                                     overlay_bi *ov,
                                                                     resource_usage_bi *ru,
                                                                     batteryinfo_bi *bi)
{
    click_result_bi result;

    for (size_t i = 0; i < hits.size(); ++i)
    {
        const hit_bi &h = hits[i];

        if (h.kind != HIT_TAB && h.rect.top < TABBAR_H)
            continue;

        if (!inRect(h.rect, cursorPos))
            continue;

        result.handled = true;

        switch (h.kind)
        {
        case HIT_TAB:
            setTab((selected_option)h.index);
            break;

        case HIT_TOGGLE:
        case HIT_ROW_CHIP:
        case HIT_GRAPH_CHIP:
            if (h.state)
            {
                bool wasAutostart = ru ? ru->start_With_Windows : false;
                bool wasAdmin = ru ? ru->start_As_Admin : false;

                *h.state = !*h.state;

                if (ru && ru->start_As_Admin != wasAdmin)
                    result.toggledAdmin = true;
                else if (ru && ru->start_With_Windows != wasAutostart)
                    result.toggledAutostart = true;

                result.needsSave = true;
            }
            break;

        case HIT_RAIL:
            setSettingsGroup(h.index);
            break;

        case HIT_PRESET:
            applyPreset(h.index, ov, ru, bi);
            result.needsSave = true;
            break;

        case HIT_THEME:
            nightMode = (h.index == 1);
            result.needsBrushRebuild = true;
            result.needsSave = true;
            break;

        case HIT_ACCENT:
            if (h.index >= 0 && h.index < (int)colorPalette.size())
            {
                accentColor = colorPalette[h.index];
                result.needsBrushRebuild = true;
                result.needsSave = true;
            }
            break;

        case HIT_CORNER:
            if (ov)
            {
                ov->corner = (overlay_bi::corner_bi)h.index;
                ov->UpdatePosition();
                result.needsSave = true;
            }
            break;

        case HIT_REFRESH:
            if (ov && h.index >= 30 && h.index <= 2000)
            {
                ov->refreshMs = h.index;
                result.needsSave = true;
                result.refreshChanged = true;
            }
            break;

        case HIT_MARGIN:
            if (ov)
            {
                ov->margin += h.index * 2;
                if (ov->margin < 0)
                    ov->margin = 0;
                if (ov->margin > 200)
                    ov->margin = 200;

                ov->UpdatePosition();
                result.needsSave = true;
            }
            break;

        case HIT_SCALE:
            if (ov)
            {
                ov->setScale(ov->getScale() + h.index * 25);
                result.needsSave = true;
            }
            break;

        case HIT_MEMUNIT:
            if (ru && h.index >= resource_usage_bi::MEM_UNIT_AUTO &&
                h.index <= resource_usage_bi::MEM_UNIT_GB)
            {
                ru->memUnit = h.index;
                result.needsSave = true;
            }
            break;

        case HIT_CAPTURE_RUN:
            selectedRun = (selectedRun == h.index) ? -1 : h.index;
            break;

        case HIT_USERPRESET_APPLY:
            if (h.index >= 0 && h.index < 3 && !userPreset[h.index].empty())
            {
                applyLayout(userPreset[h.index], ov, ru, bi);
                result.needsSave = true;
            }
            break;

        case HIT_USERPRESET_SAVE:
            if (h.index >= 0 && h.index < 3)
            {
                userPreset[h.index] = captureLayout(ov, ru, bi);
                result.needsSave = true;
            }
            break;

        case HIT_ACTION:
            result.action = h.index;
            break;

        default:
            result.handled = false;
            break;
        }

        if (result.needsSave)
            settingsDirty = true;

        if (result.handled)
            return result;
    }

    return result;
}
