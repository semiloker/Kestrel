#include "draw_batteryinfo_bi.h"

bool draw_batteryinfo_bi::initBrush(ID2D1HwndRenderTarget *pRT)
{
    return updateBrushes(pRT);
}

bool draw_batteryinfo_bi::updateBrushes(ID2D1HwndRenderTarget *pRT)
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

    if (nightMode)
    {
        backgroundColor = D2D1::ColorF(0.06f, 0.06f, 0.07f);
        textColor = D2D1::ColorF(0.92f, 0.92f, 0.92f);
        labelColor = D2D1::ColorF(0.78f, 0.78f, 0.78f);
        separatorColor = D2D1::ColorF(0.18f, 0.18f, 0.18f);
    }
    else
    {
        backgroundColor = D2D1::ColorF(0.98f, 0.98f, 0.98f);
        textColor = D2D1::ColorF(0.1f, 0.1f, 0.1f);
        labelColor = D2D1::ColorF(0.4f, 0.4f, 0.4f);
        separatorColor = D2D1::ColorF(0.8f, 0.8f, 0.8f);
    }

    pRT->CreateSolidColorBrush(labelColor, &pLabelBrush);
    pRT->CreateSolidColorBrush(textColor, &pValueBrush);
    pRT->CreateSolidColorBrush(headerColor, &pHeaderBrush);
    pRT->CreateSolidColorBrush(separatorColor, &pSeparatorBrush);

    pRT->CreateSolidColorBrush(D2D1::ColorF(0.2f, 0.6f, 0.3f), &pSwitchOnBrush);
    pRT->CreateSolidColorBrush(D2D1::ColorF(0.7f, 0.7f, 0.7f), &pSwitchOffBrush);
    pRT->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f), &pSwitchKnobBrush);

    pRT->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::DarkGray), &pScrollBarBrush);

    pRT->CreateSolidColorBrush(backgroundColor, &pBackgroundBrush);

    pRT->CreateSolidColorBrush(accentColor, &pAccentBrush);

    return true;
}

bool draw_batteryinfo_bi::clearBackground(ID2D1HwndRenderTarget *pRT)
{
    pRT->Clear(backgroundColor);
    return true;
}

bool draw_batteryinfo_bi::isCursorInBatteryStatus(POINT cursorPos)
{
    return (cursorPos.x >= rectBatteryStatus.left && cursorPos.x <= rectBatteryStatus.right &&
            cursorPos.y >= rectBatteryStatus.top && cursorPos.y <= rectBatteryStatus.bottom);
}

bool draw_batteryinfo_bi::isCursorInSettings(POINT cursorPos)
{
    return (cursorPos.x >= rectSettings.left && cursorPos.x <= rectSettings.right &&
            cursorPos.y >= rectSettings.top && cursorPos.y <= rectSettings.bottom);
}

bool draw_batteryinfo_bi::isCursorInAboutMe(POINT cursorPos)
{
    return (cursorPos.x >= rectAboutMe.left && cursorPos.x <= rectAboutMe.right &&
            cursorPos.y >= rectAboutMe.top && cursorPos.y <= rectAboutMe.bottom);
}

bool draw_batteryinfo_bi::isCursorInAppearance(POINT cursorPos)
{
    return (cursorPos.x >= rectAppearance.left && cursorPos.x <= rectAppearance.right &&
            cursorPos.y >= rectAppearance.top && cursorPos.y <= rectAppearance.bottom);
}

void draw_batteryinfo_bi::drawHeaders(ID2D1HwndRenderTarget *pRT, init_dwrite_bi *initdwrite_bi, int startX, int startY, int lineHeight)
{
    std::wstring header_battery_status = L"Battery Status";
    std::wstring header_settings = L"Settings";
    std::wstring header_about_me = L"About Me";
    std::wstring header_appearance = L"Appearance";

    float currentX = (float)startX;
    float currentY = (float)startY;

    int headerIndex = 0;

    pRT->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    auto drawHeaderWithBox = [&](const std::wstring &text, bool isSelected)
    {
        IDWriteTextLayout *pTextLayout = nullptr;
        HRESULT hr = initdwrite_bi->pDWriteFactory->CreateTextLayout(
            text.c_str(),
            static_cast<UINT32>(text.length()),
            initdwrite_bi->pTextFormatHeader,
            1000.0f,
            100.0f,
            &pTextLayout);

        if (SUCCEEDED(hr) && pTextLayout)
        {
            DWRITE_TEXT_METRICS metrics;
            if (SUCCEEDED(pTextLayout->GetMetrics(&metrics)))
            {
                float width = metrics.width;
                float height = metrics.height;

                D2D1_RECT_F rect = D2D1::RectF(currentX, currentY, currentX + width, currentY + height);

                if (headerIndex == 0)
                    rectBatteryStatus = rect;
                else if (headerIndex == 1)
                    rectSettings = rect;
                else if (headerIndex == 2)
                    rectAppearance = rect;
                else if (headerIndex == 3)
                    rectAboutMe = rect;

                D2D1_COLOR_F currentTextColor = (isSelected) ? accentColor : D2D1::ColorF(0.5f, 0.5f, 0.5f);

                ID2D1SolidColorBrush *pTextBrush;
                pRT->CreateSolidColorBrush(currentTextColor, &pTextBrush);

                pRT->DrawText(
                    text.c_str(),
                    static_cast<UINT32>(text.length()),
                    initdwrite_bi->pTextFormatHeader,
                    rect,
                    pTextBrush);

                currentX += width + 30.0f;
                headerIndex++;

                pTextBrush->Release();
            }
            pTextLayout->Release();
        }
    };

    D2D1_RECT_F box_header = D2D1::RectF(
        0.0f, 0.0f, maxWidth, 60.0f);

    if (pBackgroundBrush)
        pRT->FillRectangle(&box_header, pBackgroundBrush);

    drawHeaderWithBox(header_battery_status, selectedTab == BATTERY_INFO);
    drawHeaderWithBox(header_settings, selectedTab == SETTINGS);
    drawHeaderWithBox(header_appearance, selectedTab == APPEARANCE);
    drawHeaderWithBox(header_about_me, selectedTab == ABOUT_ME);
}

void draw_batteryinfo_bi::drawHeaderBatteryInfoD2D(ID2D1HwndRenderTarget *pRT, batteryinfo_bi *bi_bi, init_dwrite_bi *initdwrite_bi, int startX, int startY, int lineHeight)
{
    D2D1_SIZE_F rtSize = pRT->GetSize();
    maxWidth = rtSize.width;

    std::map<std::wstring, std::vector<std::pair<std::wstring, std::wstring>>> categories = {
        {L"Basic Info", {
                            {L"Chemistry", std::wstring(bi_bi->info_static.Chemistry.begin(), bi_bi->info_static.Chemistry.end())},
                            {L"Power state", std::wstring(bi_bi->info_1s.PowerState.begin(), bi_bi->info_1s.PowerState.end())},
                        }},
        {L"Capacity", {
                          {L"Designed capacity", std::wstring(bi_bi->info_static.DesignedCapacity.begin(), bi_bi->info_static.DesignedCapacity.end())},
                          {L"Full charged capacity", std::wstring(bi_bi->info_static.FullChargedCapacity.begin(), bi_bi->info_static.FullChargedCapacity.end())},
                          {L"Remaining capacity", std::wstring(bi_bi->info_1s.RemainingCapacity.begin(), bi_bi->info_1s.RemainingCapacity.end())},
                          {L"Charge level", std::wstring(bi_bi->info_1s.ChargeLevel.begin(), bi_bi->info_1s.ChargeLevel.end())},
                          {L"Wear level", std::wstring(bi_bi->info_static.WearLevel.begin(), bi_bi->info_static.WearLevel.end())},
                          {L"Cycle count", std::wstring(bi_bi->info_static.CycleCount.begin(), bi_bi->info_static.CycleCount.end())},
                      }},
        {L"Voltage & Rate", {
                                {L"Voltage", std::wstring(bi_bi->info_1s.Voltage.begin(), bi_bi->info_1s.Voltage.end())},
                                {L"Rate", std::wstring(bi_bi->info_1s.Rate.begin(), bi_bi->info_1s.Rate.end())},
                            }},
        {L"Alerts", {
                        {L"Default alert 1", std::wstring(bi_bi->info_static.DefaultAlert1.begin(), bi_bi->info_static.DefaultAlert1.end())},
                        {L"Default alert 2", std::wstring(bi_bi->info_static.DefaultAlert2.begin(), bi_bi->info_static.DefaultAlert2.end())},
                    }},
        {L"Time Remaining", {
                                {L"Time to 0%", std::wstring(bi_bi->info_10s.TimeRemaining.begin(), bi_bi->info_10s.TimeRemaining.end())},
                                {L"Time to full charge", std::wstring(bi_bi->info_10s.TimeToFullCharge.begin(), bi_bi->info_10s.TimeToFullCharge.end())},
                            }}};

    int y = startY;

    y += lineHeight + 16;

    for (const auto &category : categories)
    {
        pRT->DrawText(
            category.first.c_str(),
            static_cast<UINT32>(category.first.length()),
            initdwrite_bi->pTextFormatLabel,
            D2D1::RectF((FLOAT)startX, (FLOAT)y, maxWidth, (FLOAT)(y + lineHeight)),
            pAccentBrush);
        y += lineHeight + 4;

        for (const auto &field : category.second)
        {
            std::wstring line = field.first + L" - " + field.second;

            pRT->DrawText(
                line.c_str(),
                static_cast<UINT32>(line.length()),
                initdwrite_bi->pTextFormatValue,
                D2D1::RectF((FLOAT)startX, (FLOAT)y, maxWidth, (FLOAT)(y + lineHeight)),
                pValueBrush);
            y += lineHeight;

            pRT->DrawLine(
                D2D1::Point2F((FLOAT)startX, (FLOAT)(y + 2)),
                D2D1::Point2F(maxWidth - startX, (FLOAT)(y + 2)),
                pSeparatorBrush,
                0.5f);
            y += 8;
        }
        y += 12;
    }
}

void draw_batteryinfo_bi::drawToggleSwitch(ID2D1HwndRenderTarget *pRT, init_dwrite_bi *initdwrite_bi,
                                           float x, float y, bool &toggleState, const std::wstring &labelText)
{
    D2D1_RECT_F textRect = D2D1::RectF(x, y, x + 250, y + 24);
    pRT->DrawText(
        labelText.c_str(),
        (UINT32)labelText.length(),
        initdwrite_bi->pTextFormatLabel,
        textRect,
        pLabelBrush);

    const float switchWidth = 48.0f;
    const float switchHeight = 24.0f;
    const float knobSize = switchHeight - 6.0f;
    float switchX = maxWidth - 70.0f;

    float textHeight = 24.0f;
    float switchY = y + (textHeight - switchHeight) / 2.0f;

    D2D1_ROUNDED_RECT switchRect = {
        D2D1::RectF(switchX, switchY, switchX + switchWidth, switchY + switchHeight),
        switchHeight / 2,
        switchHeight / 2};

    SwitchRect hitRect =
        {
            switchRect.rect,
            &toggleState};
    switchRects.push_back(hitRect);

    pRT->FillRoundedRectangle(switchRect, toggleState ? pSwitchOnBrush : pSwitchOffBrush);

    float knobX = toggleState ? (switchX + switchWidth - knobSize - 3.0f) : (switchX + 3.0f);

    D2D1_ELLIPSE knob = {
        D2D1::Point2F(knobX + knobSize / 2, switchY + switchHeight / 2),
        knobSize / 2,
        knobSize / 2};

    pRT->FillEllipse(knob, pSwitchKnobBrush);

    if (toggleState)
    {
        ID2D1SolidColorBrush *pHighlightBrush = nullptr;
        pRT->CreateSolidColorBrush(D2D1::ColorF(0.3f, 0.7f, 0.4f), &pHighlightBrush);

        D2D1_ROUNDED_RECT highlightRect = {
            D2D1::RectF(switchX + 1, switchY + 1, switchX + switchWidth - 1, switchY + switchHeight - 1),
            (switchHeight - 2) / 2,
            (switchHeight - 2) / 2};

        pRT->DrawRoundedRectangle(highlightRect, pHighlightBrush, 1.0f);
        pHighlightBrush->Release();
    }
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

bool draw_batteryinfo_bi::handleSwitchClick(POINT cursorPos)
{
    for (size_t i = 0; i < switchRects.size(); ++i)
    {
        const D2D1_RECT_F &r = switchRects[i].rect;
        if (cursorPos.x >= r.left && cursorPos.x <= r.right &&
            cursorPos.y >= r.top && cursorPos.y <= r.bottom)
        {
            if (switchRects[i].pState)
            {
                *(switchRects[i].pState) = !*(switchRects[i].pState);
                return true;
            }
            else
            {
                OutputDebugStringA("handleSwitchClick: null pState\n");
                return false;
            }
        }
    }
    return false;
}

bool draw_batteryinfo_bi::handleColorClick(POINT cursorPos)
{
    size_t n = std::min(colorRects.size(), colorPalette.size());
    for (size_t i = 0; i < n; ++i)
    {
        const D2D1_RECT_F &r = colorRects[i];
        if (cursorPos.x >= r.left && cursorPos.x <= r.right &&
            cursorPos.y >= r.top && cursorPos.y <= r.bottom)
        {
            accentColor = colorPalette[i];
            return true;
        }
    }
    return false;
}

bool draw_batteryinfo_bi::handleCornerClick(POINT cursorPos, overlay_bi *ov_bi)
{
    if (!ov_bi)
        return false;

    for (size_t i = 0; i < cornerRects.size(); ++i)
    {
        const D2D1_RECT_F &r = cornerRects[i];
        if (cursorPos.x >= r.left && cursorPos.x <= r.right &&
            cursorPos.y >= r.top && cursorPos.y <= r.bottom)
        {
            ov_bi->corner = (overlay_bi::corner_bi)i;

            ov_bi->Render();
            return true;
        }
    }
    return false;
}

bool draw_batteryinfo_bi::handleAppearanceClick(POINT cursorPos, overlay_bi *ov_bi)
{
    if (handleSwitchClick(cursorPos))
    {
        OutputDebugStringA("Appearance: switch toggled\n");
        return true;
    }
    if (handleColorClick(cursorPos))
    {
        OutputDebugStringA("Appearance: color chosen\n");
        return true;
    }
    if (handleCornerClick(cursorPos, ov_bi))
    {
        OutputDebugStringA("Appearance: HUD corner chosen\n");
        return true;
    }
    return false;
}

void draw_batteryinfo_bi::drawHeaderSettingsD2D(ID2D1HwndRenderTarget *pRT, init_dwrite_bi *initdwrite_bi, overlay_bi *ov_bi, resource_usage_bi *ru_bi, batteryinfo_bi *bi_bi)
{
    D2D1_SIZE_F rtSize = pRT->GetSize();
    maxWidth = rtSize.width;
    viewHeight = rtSize.height;

    switchRects.clear();

    float y = 66.0f - scrollOffsetY;

    std::wstring displayGroup = L"Overlay";
    pRT->DrawText(displayGroup.c_str(), (UINT32)displayGroup.length(),
                  initdwrite_bi->pTextFormatValue,
                  D2D1::RectF(20, y, maxWidth, y + 20), pAccentBrush);
    y += 30;

    drawToggleSwitch(pRT, initdwrite_bi, 40, y, ov_bi->show_on_screen_display, L"Show On-Screen Display");
    y += 40;

    y += 10;
    if (ov_bi->show_on_screen_display == true)
    {
        drawToggleSwitch(pRT, initdwrite_bi, 80, y, ov_bi->hud.showDisplay, L"Display Info Row");
        y += 50;

        pRT->DrawLine(D2D1::Point2F(20, y), D2D1::Point2F(maxWidth - 20, y), pSeparatorBrush);
        y += 20;

        std::wstring frameGroup = ov_bi->hud.metrics[HUD_M_FPS].available
                                      ? L"Frame Timing"
                                      : L"Frame Timing  (needs admin)";
        pRT->DrawText(frameGroup.c_str(), (UINT32)frameGroup.length(),
                      initdwrite_bi->pTextFormatValue,
                      D2D1::RectF(40, y, maxWidth, y + 20), pValueBrush);
        y += 30;

        drawToggleSwitch(pRT, initdwrite_bi, 80, y, ov_bi->hud.metrics[HUD_M_FPS].show, L"FPS");
        y += 40;
        drawToggleSwitch(pRT, initdwrite_bi, 80, y, ov_bi->hud.metrics[HUD_M_PRE].show, L"Frame Interval");
        y += 40;
        drawToggleSwitch(pRT, initdwrite_bi, 80, y, ov_bi->hud.metrics[HUD_M_PRE].graphed, L"Frame Interval on graph");
        y += 40;
        drawToggleSwitch(pRT, initdwrite_bi, 80, y, ov_bi->hud.metrics[HUD_M_GPUMS].show, L"GPU ms");
        y += 40;
        drawToggleSwitch(pRT, initdwrite_bi, 80, y, ov_bi->hud.metrics[HUD_M_GPUMS].graphed, L"GPU ms on graph");
        y += 60;

        pRT->DrawLine(D2D1::Point2F(20, y), D2D1::Point2F(maxWidth - 20, y), pSeparatorBrush);
        y += 20;

        std::wstring gpuGroup = L"GPU";
        pRT->DrawText(gpuGroup.c_str(), (UINT32)gpuGroup.length(),
                      initdwrite_bi->pTextFormatValue,
                      D2D1::RectF(40, y, maxWidth, y + 20), pValueBrush);
        y += 30;

        drawToggleSwitch(pRT, initdwrite_bi, 80, y, ru_bi->gpuInfo.show_gpuLoad, L"GPU Usage Percent");
        y += 40;
        drawToggleSwitch(pRT, initdwrite_bi, 80, y, ov_bi->hud.metrics[HUD_M_GPU].graphed, L"GPU Usage on graph");
        y += 40;
        drawToggleSwitch(pRT, initdwrite_bi, 80, y, ru_bi->gpuInfo.show_gpuName, L"GPU Name");
        y += 60;

        pRT->DrawLine(D2D1::Point2F(20, y), D2D1::Point2F(maxWidth - 20, y), pSeparatorBrush);
        y += 20;

        std::wstring cpuGroup = L"CPU";
        pRT->DrawText(cpuGroup.c_str(), (UINT32)cpuGroup.length(),
                      initdwrite_bi->pTextFormatValue,
                      D2D1::RectF(40, y, maxWidth, y + 20), pValueBrush);
        y += 30;

        drawToggleSwitch(pRT, initdwrite_bi, 80, y, ru_bi->cpuInfo.show_UsagePercent, L"CPU Usage Percent");
        y += 40;
        drawToggleSwitch(pRT, initdwrite_bi, 80, y, ov_bi->hud.metrics[HUD_M_CPU].graphed, L"CPU Usage on graph");
        y += 40;
        drawToggleSwitch(pRT, initdwrite_bi, 80, y, ru_bi->cpuInfo.show_packagePower, L"CPU Package Power");
        y += 40;
        drawToggleSwitch(pRT, initdwrite_bi, 80, y, ov_bi->hud.metrics[HUD_M_CPUW].graphed, L"CPU Package Power on graph");
        y += 40;
        drawToggleSwitch(pRT, initdwrite_bi, 80, y, ru_bi->cpuInfo.show_CoreUsagePercents, L"CPU Core Usage Percents");
        y += 40;
        drawToggleSwitch(pRT, initdwrite_bi, 80, y, ru_bi->cpuInfo.show_cpuName, L"CPU Name");
        y += 40;
        drawToggleSwitch(pRT, initdwrite_bi, 80, y, ru_bi->cpuInfo.show_architecture, L"CPU Architecture");
        y += 60;

        pRT->DrawLine(D2D1::Point2F(20, y), D2D1::Point2F(maxWidth - 20, y), pSeparatorBrush);
        y += 20;

        std::wstring batteryGroup = L"Battery";
        pRT->DrawText(batteryGroup.c_str(), (UINT32)batteryGroup.length(),
                      initdwrite_bi->pTextFormatValue,
                      D2D1::RectF(40, y, maxWidth, y + 20), pValueBrush);
        y += 30;

        drawToggleSwitch(pRT, initdwrite_bi, 80, y, bi_bi->info_1s.Voltage_, L"Battery Voltage");
        y += 40;
        drawToggleSwitch(pRT, initdwrite_bi, 80, y, bi_bi->info_1s.Rate_, L"Battery Rate");
        y += 40;
        drawToggleSwitch(pRT, initdwrite_bi, 80, y, bi_bi->info_1s.PowerState_, L"Battery Power State");
        y += 40;
        drawToggleSwitch(pRT, initdwrite_bi, 80, y, bi_bi->info_1s.RemainingCapacity_, L"Battery Remaining Capacity");
        y += 40;
        drawToggleSwitch(pRT, initdwrite_bi, 80, y, bi_bi->info_1s.ChargeLevel_, L"Battery Charge Level");
        y += 40;
        drawToggleSwitch(pRT, initdwrite_bi, 80, y, bi_bi->info_10s.TimeRemaining_, L"Battery Remaining Time");
        y += 60;

        pRT->DrawLine(D2D1::Point2F(20, y), D2D1::Point2F(maxWidth - 20, y), pSeparatorBrush);
        y += 20;

        std::wstring ramGroup = L"Ram";
        pRT->DrawText(ramGroup.c_str(), (UINT32)ramGroup.length(),
                      initdwrite_bi->pTextFormatValue,
                      D2D1::RectF(40, y, maxWidth, y + 20), pValueBrush);
        y += 30;

        drawToggleSwitch(pRT, initdwrite_bi, 80, y, ru_bi->ramInfo.show_dwMemoryLoad, L"Memory Load");
        y += 40;
        drawToggleSwitch(pRT, initdwrite_bi, 80, y, ov_bi->hud.metrics[HUD_M_RAM].graphed, L"Memory Load on graph");
        y += 40;
        drawToggleSwitch(pRT, initdwrite_bi, 80, y, ru_bi->ramInfo.show_ullTotalPhys, L"Total Physical RAM");
        y += 40;
        drawToggleSwitch(pRT, initdwrite_bi, 80, y, ru_bi->ramInfo.show_ullAvailPhys, L"Available Physical RAM");
        y += 40;
        drawToggleSwitch(pRT, initdwrite_bi, 80, y, ru_bi->ramInfo.show_ullTotalPageFile, L"Commit Charge");
        y += 40;
        drawToggleSwitch(pRT, initdwrite_bi, 80, y, ov_bi->hud.metrics[HUD_M_COMMIT].graphed, L"Commit Charge on graph");
        y += 40;
        drawToggleSwitch(pRT, initdwrite_bi, 80, y, ru_bi->ramInfo.show_ullAvailPageFile, L"Available Page File");
        y += 40;
        drawToggleSwitch(pRT, initdwrite_bi, 80, y, ru_bi->ramInfo.show_ullTotalVirtual, L"Total Virtual Memory");
        y += 40;
        drawToggleSwitch(pRT, initdwrite_bi, 80, y, ru_bi->ramInfo.show_ullAvailVirtual, L"Available Virtual Memory");
        y += 40;
        drawToggleSwitch(pRT, initdwrite_bi, 80, y, ru_bi->ramInfo.show_ullAvailExtendedVirtual, L"Extended Virtual Memory");
        y += 60;
    }
    pRT->DrawLine(D2D1::Point2F(20, y), D2D1::Point2F(maxWidth - 20, y), pSeparatorBrush);
    y += 20;

    std::wstring behaviorGroup = L"Behavior";
    pRT->DrawText(behaviorGroup.c_str(), (UINT32)behaviorGroup.length(),
                  initdwrite_bi->pTextFormatValue,
                  D2D1::RectF(20, y, maxWidth, y + 20), pAccentBrush);
    y += 30;

    drawToggleSwitch(pRT, initdwrite_bi, 40, y, ru_bi->start_With_Windows, L"Start with Windows");
    y += 40;

    drawToggleSwitch(pRT, initdwrite_bi, 40, y, ru_bi->start_As_Admin,
                     L"Run as administrator (needed for FPS)");
    y += 40;

    drawToggleSwitch(pRT, initdwrite_bi, 40, y, ru_bi->minimize_To_Tray, L"Minimize to tray");
    y += 40;
    drawToggleSwitch(pRT, initdwrite_bi, 40, y, ru_bi->exit_on_key_esc, L"Exit on key 'ESC'");
    y += 60;

    contentHeight = y + scrollOffsetY;
    clampScroll();

    float topOffset = 60.0f;

    if (contentHeight > rtSize.height - topOffset)
    {
        float visibleHeight = rtSize.height - topOffset;
        float barHeight = ((visibleHeight / contentHeight) * visibleHeight) + topOffset;
        float barY = topOffset + (scrollOffsetY / contentHeight) * visibleHeight;

        D2D1_RECT_F scrollbarRect = D2D1::RectF(
            rtSize.width - 5,
            barY,
            rtSize.width - 1,
            barY + barHeight);

        pRT->FillRectangle(&scrollbarRect, pScrollBarBrush);
    }
}

void draw_batteryinfo_bi::drawHeaderAppearanceD2D(ID2D1HwndRenderTarget *pRT, init_dwrite_bi *initdwrite_bi, overlay_bi *ov_bi)
{
    D2D1_SIZE_F rtSize = pRT->GetSize();
    maxWidth = rtSize.width;
    viewHeight = rtSize.height;

    switchRects.clear();
    colorRects.clear();
    cornerRects.clear();

    float y = 66.0f - scrollOffsetY;

    std::wstring appearanceGroup = L"Appearance";
    pRT->DrawText(appearanceGroup.c_str(), (UINT32)appearanceGroup.length(),
                  initdwrite_bi->pTextFormatValue,
                  D2D1::RectF(20, y, maxWidth, y + 20), pAccentBrush);
    y += 30;

    drawToggleSwitch(pRT, initdwrite_bi, 40, y, nightMode, L"Night mode");
    y += 60;

    pRT->DrawLine(D2D1::Point2F(20, y), D2D1::Point2F(maxWidth - 20, y), pSeparatorBrush);
    y += 20;

    std::wstring colorLabel = L"Accent color";
    pRT->DrawText(colorLabel.c_str(), (UINT32)colorLabel.length(),
                  initdwrite_bi->pTextFormatValue,
                  D2D1::RectF(20, y, maxWidth, y + 20), pAccentBrush);
    y += 28;

    const float boxSize = 28.0f;
    const float gap = 12.0f;
    const float leftMargin = 40.0f;
    const int cols = 8;

    float boxX = leftMargin;
    float startX = leftMargin;
    int col = 0;

    for (size_t i = 0; i < colorPalette.size(); ++i)
    {
        if (col >= cols)
        {
            col = 0;
            boxX = startX;
            y += boxSize + gap;
        }

        D2D1_RECT_F box = D2D1::RectF(boxX, y, boxX + boxSize, y + boxSize);
        colorRects.push_back(box);

        ID2D1SolidColorBrush *pTmp = nullptr;
        pRT->CreateSolidColorBrush(colorPalette[i], &pTmp);
        pRT->FillRectangle(&box, pTmp);

        const float eps = 0.01f;
        if (std::fabs(colorPalette[i].r - accentColor.r) < eps &&
            std::fabs(colorPalette[i].g - accentColor.g) < eps &&
            std::fabs(colorPalette[i].b - accentColor.b) < eps)
        {
            pRT->DrawRectangle(&box, pHeaderBrush, 2.0f);
        }

        pTmp->Release();

        boxX += boxSize + gap;
        ++col;
    }

    y += boxSize + 24;

    pRT->DrawLine(D2D1::Point2F(20, y), D2D1::Point2F(maxWidth - 20, y), pSeparatorBrush);
    y += 20;

    std::wstring cornerLabel = L"HUD corner";
    pRT->DrawText(cornerLabel.c_str(), (UINT32)cornerLabel.length(),
                  initdwrite_bi->pTextFormatValue,
                  D2D1::RectF(20, y, maxWidth, y + 20), pAccentBrush);
    y += 28;

    const wchar_t *cornerNames[] = {L"Top left", L"Top right", L"Bottom left", L"Bottom right"};
    const float cornerWidth = 110.0f;
    const float cornerHeight = 28.0f;
    const float cornerGap = 8.0f;

    float cornerX = 40.0f;
    for (int i = 0; i < 4; ++i)
    {
        D2D1_RECT_F box = D2D1::RectF(cornerX, y, cornerX + cornerWidth, y + cornerHeight);
        cornerRects.push_back(box);

        bool selected = (ov_bi && (int)ov_bi->corner == i);
        pRT->DrawRectangle(&box, selected ? pAccentBrush : pSeparatorBrush, selected ? 2.0f : 1.0f);

        pRT->DrawText(cornerNames[i], (UINT32)wcslen(cornerNames[i]),
                      initdwrite_bi->pTextFormatValue,
                      D2D1::RectF(cornerX + 8, y + 5, cornerX + cornerWidth, y + cornerHeight),
                      selected ? pAccentBrush : pValueBrush);

        cornerX += cornerWidth + cornerGap;
    }

    y += cornerHeight + 24;

    contentHeight = y + scrollOffsetY;
    clampScroll();
}

void draw_batteryinfo_bi::drawHeaderAboutMeD2D(ID2D1HwndRenderTarget *pRT, init_dwrite_bi *initdwrite_bi, overlay_bi *ov_bi, resource_usage_bi *ru_bi, batteryinfo_bi *bi_bi)
{
    D2D1_SIZE_F rtSize = pRT->GetSize();
    maxWidth = rtSize.width;

    float y = 66.0f - scrollOffsetY;

    std::wstring behaviorGroup = L"About Me";
    pRT->DrawText(
        behaviorGroup.c_str(),
        static_cast<UINT32>(behaviorGroup.length()),
        initdwrite_bi->pTextFormatLabel,
        D2D1::RectF((FLOAT)20, (FLOAT)y, maxWidth, y + 20),
        pAccentBrush);
    y += 30;
    std::wstring info = L"==БЬІЛО И БЬІЛО==";
    pRT->DrawText(info.c_str(), (UINT32)info.length(),
                  initdwrite_bi->pTextFormatValue,
                  D2D1::RectF(20, y, maxWidth, y + 20), pLabelBrush);
}
