#include "main.h"
#include "resource_bi.h"
#include "autostart_bi.h"
#include "logger_bi.h"
#include "paths_bi.h"
#include "dpi_bi.h"
#include "app_identity_bi.h"

#include <cstring>

const char win_bi::szClassName[] = APP_WINDOW_CLASS;

#define HOTKEY_TOGGLE_HUD 1

#define WINDOW_DIP_W 550
#define WINDOW_DIP_H 750

win_bi::win_bi(HINSTANCE hInstance) : hInstance(hInstance), hwnd(NULL)
{
    ZeroMemory(&nid, sizeof(nid));
}

bool win_bi::Register()
{
    hAppIcon = (HICON)LoadImageA(hInstance, MAKEINTRESOURCEA(IDI_APPICON),
                                 IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    hAppIconSmall = (HICON)LoadImageA(hInstance, MAKEINTRESOURCEA(IDI_APPICON),
                                      IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);

    if (!hAppIcon)
    {
        log_bi::writeErr(GetLastError(), "icon: LoadImage from resources failed");
        hAppIcon = LoadIcon(NULL, IDI_APPLICATION);
    }
    if (!hAppIconSmall)
        hAppIconSmall = hAppIcon;

    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = 0;
    wc.lpfnWndProc = win_bi::WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    wc.hIcon = hAppIcon;
    wc.hIconSm = hAppIconSmall;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = szClassName;

    return RegisterClassEx(&wc) != 0;
}

bool win_bi::Create(int nCmdShow)
{
    bi_bi.reset(new batteryinfo_bi());
    initd2d1_bi.reset(new init_d2d1_bi());
    initdwrite_bi.reset(new init_dwrite_bi());
    draw_bibi_bi.reset(new draw_batteryinfo_bi());
    ru_bi.reset(new resource_usage_bi());
    ov_bi.reset(new overlay_bi());

    ov_bi->margin = initdwrite_bi->overlay_pos_x;
    ov_bi->hud.initStaticInfo(ru_bi->gpuInfo.gpuName);

    settings.load();
    settings.applyTo(ru_bi.get(), ov_bi.get(), draw_bibi_bi.get(), bi_bi.get());

    updater.reset(new update_bi());

    etwTrace.reset(new etw_bi());

    int fbProv = settings.getInt("etw.fallbackProvider", -1);
    int fbId = settings.getInt("etw.fallbackEventId", 0);
    if (fbProv >= 0 && fbProv < etw_bi::PROV_COUNT && fbId > 0)
        etwTrace->setFallbackSource((etw_bi::provider_bi)fbProv, (unsigned)fbId);

    etwTrace->setDeepCensus(settings.getBool("etw.deepCensus", false));

    etwTrace->start();

    bool frameDataOk = etwTrace->running();
    ov_bi->hud.metrics[HUD_M_FPS].available = frameDataOk;
    ov_bi->hud.metrics[HUD_M_PRE].available = frameDataOk;
    ov_bi->hud.metrics[HUD_M_GPUMS].available = frameDataOk;

    if (!frameDataOk)
        log_bi::write("frame metrics disabled: %s", etwTrace->lastError());

    float scale = (float)dpi_bi::forSystem() / 96.0f;

    RECT frame = {0, 0,
                  (LONG)(WINDOW_DIP_W * scale + 0.5f),
                  (LONG)(WINDOW_DIP_H * scale + 0.5f)};
    AdjustWindowRectEx(&frame, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_CLIENTEDGE);

    int windowWidth = frame.right - frame.left;
    int windowHeight = frame.bottom - frame.top;

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenWidth - windowWidth) / 2;
    int y = (screenHeight - windowHeight) / 2;

    hwnd = CreateWindowEx(
        WS_EX_CLIENTEDGE, szClassName, szClassName, WS_OVERLAPPEDWINDOW,
        x, y, windowWidth, windowHeight,
        NULL, NULL, hInstance, this);

    if (!hwnd)
    {
        log_bi::writeErr(GetLastError(), "CreateWindowEx failed");
        MessageBox(NULL, "Could not create a window!", "Error", MB_ICONEXCLAMATION | MB_OK);
        return false;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    return true;
}

WPARAM win_bi::RunMessageLoop()
{
    MSG Msg;
    while (GetMessage(&Msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&Msg);
        DispatchMessage(&Msg);
    }
    return Msg.wParam;
}

LRESULT CALLBACK win_bi::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    win_bi *pThis = nullptr;

    if (msg == WM_NCCREATE)
    {
        CREATESTRUCT *pCreate = (CREATESTRUCT *)lParam;
        pThis = (win_bi *)pCreate->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
    }
    else
    {
        pThis = (win_bi *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    }

    if (!pThis)
    {
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    if (pThis->destroyed && msg != WM_DESTROY)
    {
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    switch (msg)
    {
    case WM_COMMAND:
        pThis->OnCommand(wParam);
        break;
    case WM_GETMINMAXINFO:
        pThis->OnGetMinMaxInfo(lParam);
        break;
    case WM_CREATE:
        pThis->OnCreate(hwnd);
        break;
    case WM_SIZE:
        pThis->OnResize(wParam);
        break;
    case WM_PAINT:
        pThis->OnPaint(hwnd);
        break;
    case WM_DPICHANGED:
        pThis->OnDpiChanged(wParam, lParam);
        break;
    case WM_HOTKEY:
        pThis->OnHotKey(wParam);
        break;
    case WM_KEYDOWN:
        pThis->OnKeyDown(wParam);
        break;
    case WM_KEYUP:
        pThis->OnKeyUp(wParam);
        break;
    case WM_MOUSEMOVE:
        pThis->OnMouseMove(wParam, lParam);
        break;
    case WM_MOUSEWHEEL:
        pThis->OnMouseWheel(wParam, lParam);
        break;
    case WM_LBUTTONDOWN:
        pThis->OnLeftButtonDown(wParam, lParam);
        break;
    case WM_LBUTTONUP:
        pThis->OnLeftButtonUp(wParam, lParam);
        break;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT && pThis->OnSetCursor())
            return TRUE;
        return DefWindowProc(hwnd, msg, wParam, lParam);
    case WM_APP_UPDATE:
        pThis->OnUpdateNotify();
        break;
    case WM_MOUSELEAVE:
        if (pThis->draw_bibi_bi && pThis->draw_bibi_bi->clearHover())
            InvalidateRect(hwnd, NULL, FALSE);
        break;
    case WM_RBUTTONDOWN:
        pThis->OnRightButtonDown(wParam, lParam);
        break;
    case WM_TIMER:
        pThis->OnTimer(wParam);
        break;
    case WM_SETFOCUS:
        pThis->OnSetFocus(hwnd);
        break;
    case WM_KILLFOCUS:
        pThis->OnKillFocus(hwnd);
        break;
    case WM_SYSCOMMAND:
        pThis->OnSysCommand(wParam, lParam);
        break;
    case WM_CHAR:
        pThis->OnChar(wParam);
        break;
    case WM_CLOSE:
        pThis->OnClose();
        break;
    case WM_USER + 1:
        switch (lParam)
        {
        case WM_LBUTTONDBLCLK:
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
            pThis->RemoveTrayIcon();
            break;

        case WM_RBUTTONUP:
            pThis->ShowTrayMenu();
            break;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        pThis->OnDestroy();
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

void win_bi::OnPaint(HWND hwnd)
{
    PAINTSTRUCT ps;
    BeginPaint(hwnd, &ps);

    initd2d1_bi->InitDirect2D();
    ID2D1HwndRenderTarget *pRenderTarget = initd2d1_bi->GetOrCreateRenderTarget(hwnd);
    if (pRenderTarget)
    {
        pRenderTarget->BeginDraw();

        draw_bibi_bi->initBrush(pRenderTarget);
        initdwrite_bi->InitGraph();
        draw_bibi_bi->clearBackground(pRenderTarget);

        if (draw_bibi_bi->selectedTab == draw_batteryinfo_bi::BATTERY_INFO)
        {
            draw_bibi_bi->drawBatteryTab(pRenderTarget, initdwrite_bi.get(), bi_bi.get());
        }
        else if (draw_bibi_bi->selectedTab == draw_batteryinfo_bi::SETTINGS)
        {
            draw_bibi_bi->drawSettingsTab(pRenderTarget, initdwrite_bi.get(),
                                          ov_bi.get(), ru_bi.get(), bi_bi.get());
        }
        else if (draw_bibi_bi->selectedTab == draw_batteryinfo_bi::APPEARANCE)
        {
            draw_bibi_bi->drawAppearanceTab(pRenderTarget, initdwrite_bi.get(), ov_bi.get());
        }
        else if (draw_bibi_bi->selectedTab == draw_batteryinfo_bi::ABOUT_ME)
        {
            draw_batteryinfo_bi::update_view_bi view;
            view.state = (int)updater->state();
            view.version = updater->latestVersion();
            view.message = updater->message();
            view.progress = updater->progressPercent();
            view.backupAvailable = updater->backupExists();
            view.backupVersion = updater->backupVersion();

            draw_bibi_bi->drawAboutTab(pRenderTarget, initdwrite_bi.get(),
                                       BuildDiagnostics(), view);
        }

        draw_bibi_bi->drawTabBar(pRenderTarget, initdwrite_bi.get());

        if (ov_bi)
        {
            if (ov_bi->show_on_screen_display)
                ov_bi->CreateOverlayWindow(hInstance, hwnd);
            else
                ov_bi->DestroyOverlayWindow();
        }

        HRESULT hr = pRenderTarget->EndDraw();
        if (FAILED(hr) || hr == D2DERR_RECREATE_TARGET)
        {
            draw_bibi_bi->releaseDeviceResources();
            initd2d1_bi->DiscardRenderTarget();
        }
    }
    EndPaint(hwnd, &ps);
}

void win_bi::OnDpiChanged(WPARAM wParam, LPARAM lParam)
{
    RECT *suggested = (RECT *)lParam;
    if (suggested)
    {
        SetWindowPos(hwnd, NULL,
                     suggested->left, suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    initd2d1_bi->UpdateDpi(hwnd);
    InvalidateRect(hwnd, NULL, TRUE);

    log_bi::write("dpi changed to %u", (unsigned)HIWORD(wParam));
}

void win_bi::OnHotKey(WPARAM wParam)
{
    if (wParam == HOTKEY_TOGGLE_HUD)
        ToggleOverlay();
}

void win_bi::ToggleOverlay()
{
    if (!ov_bi)
        return;

    ov_bi->show_on_screen_display = !ov_bi->show_on_screen_display;

    if (ov_bi->show_on_screen_display)
    {
        ov_bi->CreateOverlayWindow(hInstance, hwnd);
        UpdateOverlayHud();
    }
    else
    {
        ov_bi->DestroyOverlayWindow();
    }

    InvalidateRect(hwnd, NULL, TRUE);
}

void win_bi::AddTrayIcon()
{
    if (trayIconVisible)
        return;

    ZeroMemory(&nid, sizeof(NOTIFYICONDATA));
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_USER + 1;
    nid.hIcon = hAppIcon;

    if (Shell_NotifyIcon(NIM_ADD, &nid))
        trayIconVisible = true;
    else
        log_bi::writeErr(GetLastError(), "tray: Shell_NotifyIcon(NIM_ADD) failed");

    UpdateTrayTooltip();
}

void win_bi::UpdateTrayTooltip()
{
    if (!trayIconVisible || !bi_bi)
        return;

    std::string tooltip =
        "Power State: " + bi_bi->info_1s.PowerState + "\n" +
        "Charge: " + bi_bi->info_1s.ChargeLevel + "\n" +
        "Voltage: " + bi_bi->info_1s.Voltage;

    strncpy_s(nid.szTip, tooltip.c_str(), sizeof(nid.szTip) - 1);
    nid.uFlags = NIF_TIP;
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

void win_bi::RemoveTrayIcon()
{
    if (!trayIconVisible)
        return;

    Shell_NotifyIcon(NIM_DELETE, &nid);
    trayIconVisible = false;
}

void win_bi::ShowTrayMenu()
{
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    if (hMenu)
    {
        InsertMenu(hMenu, -1, MF_BYPOSITION | MF_STRING, 1, "Open");
        InsertMenu(hMenu, -1, MF_BYPOSITION | MF_STRING, 3,
                   ov_bi && ov_bi->show_on_screen_display ? "Hide overlay\tCtrl+Alt+H"
                                                          : "Show overlay\tCtrl+Alt+H");
        InsertMenu(hMenu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
        InsertMenu(hMenu, -1, MF_BYPOSITION | MF_STRING, 2, "Exit");

        SetForegroundWindow(hwnd);
        SendMessage(hwnd, WM_NULL, 0, 0);

        int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                                 pt.x, pt.y, 0, hwnd, NULL);
        DestroyMenu(hMenu);

        switch (cmd)
        {
        case 1:
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
            RemoveTrayIcon();
            break;
        case 2:
            DestroyWindow(hwnd);
            break;
        case 3:
            ToggleOverlay();
            break;
        }
    }
}

void win_bi::OnCreate(HWND hwnd)
{
    this->hwnd = hwnd;

    SetTimer(hwnd, 1, 1000, NULL);
    SetTimer(hwnd, 2, 10000, NULL);

    SetTimer(hwnd, 3, HUD_SAMPLE_INTERVAL_MS, NULL);

    if (!RegisterHotKey(hwnd, HOTKEY_TOGGLE_HUD, MOD_CONTROL | MOD_ALT, 'H'))
        log_bi::writeErr(GetLastError(), "hotkey: Ctrl+Alt+H already taken by another app");

    bool success = bi_bi->Initialize() && ru_bi->updateRam();

    if (!success)
    {
        log_bi::write("battery initialization failed (no battery, or driver refused IOCTL)");
        MessageBoxA(NULL, "Battery initialization failed!", "Error", MB_ICONERROR);
    }

    UpdateOverlayHud();
}

void win_bi::OnCommand(WPARAM wParam)
{
}

void win_bi::OnResize(WPARAM wParam)
{
    initd2d1_bi->ResizeRenderTarget(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);

    if (wParam == SIZE_MINIMIZED)
    {
        if (ru_bi && ru_bi->minimize_To_Tray)
        {
            ShowWindow(hwnd, SW_HIDE);
            AddTrayIcon();
        }
    }
}

void win_bi::UpdateOverlayHud()
{
    if (!ov_bi)
        return;

    ++hudTick;
    bool pdhTick = (hudTick % HUD_PDH_EVERY_N_TICKS) == 0;

    if (pdhTick)
    {
        ru_bi->updateHudSample();
        ru_bi->updateRam();
    }

    HWND foreground = GetForegroundWindow();
    DWORD fgPid = 0;
    if (foreground)
        GetWindowThreadProcessId(foreground, &fgPid);

    if (fgPid != 0 && fgPid != GetCurrentProcessId())
    {
        hudTargetPid = fgPid;
        ov_bi->hud.updateForeground(foreground);
    }

    etw_bi::sample_bi s;

    if (etwTrace)
    {
        etwTrace->setTarget(hudTargetPid);

        if (etwTrace->running())
            s = etwTrace->sample();
    }

    DWORD measuredPid = (s.valid && s.pid != 0) ? s.pid : hudTargetPid;

    if (etwTrace)
    {
        if (measuredPid != hudApiPid)
        {
            hudApiPid = measuredPid;
            hudApiName = etw_bi::apiName(etw_bi::detectApi(measuredPid));
        }

        ov_bi->hud.setApi(hudApiName);
    }

    double gpuBusyMs = 0.0;
    bool gpuTimeOk = pdhTick && ru_bi->updateGpuTime(measuredPid, &gpuBusyMs);

    if (etwTrace && etwTrace->running())
    {
        if (s.valid)
            lastFrameDataTick = GetTickCount();

        bool frameDataFresh = (lastFrameDataTick != 0) &&
                              ((GetTickCount() - lastFrameDataTick) < 5000);

        ov_bi->hud.metrics[HUD_M_FPS].available = frameDataFresh;
        ov_bi->hud.metrics[HUD_M_PRE].available = frameDataFresh;
        ov_bi->hud.metrics[HUD_M_GPUMS].available = frameDataFresh;

        if (s.valid)
        {
            ov_bi->hud.push(HUD_M_FPS, s.fps);
            ov_bi->hud.push(HUD_M_PRE, s.frameIntervalMs);

            if (gpuTimeOk)
            {
                double window = (double)(HUD_PDH_EVERY_N_TICKS * HUD_SAMPLE_INTERVAL_MS) / 1000.0;
                double framesThisWindow = s.fps * window;

                if (framesThisWindow > 0.01)
                {
                    lastGpuMsPerFrame = gpuBusyMs / framesThisWindow;
                    haveGpuMs = true;
                }
            }

            if (haveGpuMs)
                ov_bi->hud.push(HUD_M_GPUMS, lastGpuMsPerFrame);
        }
    }

    ov_bi->hud.metrics[HUD_M_CPUW].available = ru_bi->cpuInfo.packagePowerAvailable;
    if (ru_bi->cpuInfo.packagePowerAvailable)
        ov_bi->hud.push(HUD_M_CPUW, ru_bi->cpuInfo.packagePowerW);

    ov_bi->hud.push(HUD_M_CPU, ru_bi->cpuInfo.UsageValue);
    ov_bi->hud.push(HUD_M_GPU, ru_bi->gpuInfo.gpuLoadValue);
    ov_bi->hud.push(HUD_M_RAM, ru_bi->ramInfo.loadValue);
    ov_bi->hud.push(HUD_M_COMMIT, ru_bi->ramInfo.commitValue);

    ov_bi->hud.setMemory(ru_bi->ramInfo.usedGB, ru_bi->ramInfo.totalGB);

    ov_bi->hud.metrics[HUD_M_CPU].show = ru_bi->cpuInfo.show_UsagePercent;
    ov_bi->hud.metrics[HUD_M_GPU].show = ru_bi->gpuInfo.show_gpuLoad;
    ov_bi->hud.metrics[HUD_M_RAM].show = ru_bi->ramInfo.show_dwMemoryLoad;
    ov_bi->hud.metrics[HUD_M_COMMIT].show = ru_bi->ramInfo.show_ullTotalPageFile;
    ov_bi->hud.metrics[HUD_M_CPUW].show = ru_bi->cpuInfo.show_packagePower;
    ov_bi->hud.showDevice = ru_bi->gpuInfo.show_gpuName;
    ov_bi->hud.showMem = ru_bi->ramInfo.show_ullTotalPhys;

    ov_bi->hud.clearExtraRows();

    if (ru_bi->cpuInfo.show_cpuName)
        ov_bi->hud.addExtraRow(ru_bi->cpuInfo.cpuName);

    if (ru_bi->cpuInfo.show_architecture)
        ov_bi->hud.addExtraRow("Arch: " + ru_bi->cpuInfo.architecture);

    if (ru_bi->cpuInfo.show_CoreUsagePercents)
    {
        for (size_t i = 0; i < ru_bi->cpuInfo.CoreUsagePercents.size(); ++i)
        {
            ov_bi->hud.addExtraRow("Core " + std::to_string(i + 1) + ": " +
                                   ru_bi->cpuInfo.CoreUsagePercents[i]);
        }
    }

    if (ru_bi->ramInfo.show_ullAvailPhys)
        ov_bi->hud.addExtraRow("Avail RAM: " + ru_bi->ramInfo.ullAvailPhys);
    if (ru_bi->ramInfo.show_ullAvailPageFile)
        ov_bi->hud.addExtraRow("Avail Page: " + ru_bi->ramInfo.ullAvailPageFile);
    if (ru_bi->ramInfo.show_ullTotalVirtual)
        ov_bi->hud.addExtraRow("Total Virt: " + ru_bi->ramInfo.ullTotalVirtual);
    if (ru_bi->ramInfo.show_ullAvailVirtual)
        ov_bi->hud.addExtraRow("Avail Virt: " + ru_bi->ramInfo.ullAvailVirtual);
    if (ru_bi->ramInfo.show_ullAvailExtendedVirtual)
        ov_bi->hud.addExtraRow("Ext Virt: " + ru_bi->ramInfo.ullAvailExtendedVirtual);

    if (bi_bi->info_1s.Voltage_)
        ov_bi->hud.addExtraRow("Voltage: " + bi_bi->info_1s.Voltage);
    if (bi_bi->info_1s.Rate_)
        ov_bi->hud.addExtraRow("Rate: " + bi_bi->info_1s.Rate);
    if (bi_bi->info_1s.PowerState_)
        ov_bi->hud.addExtraRow("Power: " + bi_bi->info_1s.PowerState);
    if (bi_bi->info_1s.RemainingCapacity_)
        ov_bi->hud.addExtraRow("Remaining: " + bi_bi->info_1s.RemainingCapacity);
    if (bi_bi->info_1s.ChargeLevel_)
        ov_bi->hud.addExtraRow("Charge: " + bi_bi->info_1s.ChargeLevel);
    if (bi_bi->info_10s.TimeRemaining_)
        ov_bi->hud.addExtraRow("Time Left: " + bi_bi->info_10s.TimeRemaining);

    ov_bi->Render();
}

void win_bi::OnKeyDown(WPARAM wParam)
{
    if (wParam == VK_ESCAPE && ru_bi->exit_on_key_esc == true)
        SendMessage(hwnd, WM_CLOSE, 0, 0);
}

void win_bi::OnKeyUp(WPARAM wParam)
{
}

POINT win_bi::ClientToDip(LPARAM lParam) const
{
    POINT p;
    p.x = GET_X_LPARAM(lParam);
    p.y = GET_Y_LPARAM(lParam);

    float scale = dpi_bi::scaleForWindow(hwnd);
    if (scale > 0.01f)
    {
        p.x = (LONG)((float)p.x / scale + 0.5f);
        p.y = (LONG)((float)p.y / scale + 0.5f);
    }

    return p;
}

void win_bi::OnMouseMove(WPARAM wParam, LPARAM lParam)
{
    if (!draw_bibi_bi)
        return;

    pt = ClientToDip(lParam);

    if (draw_bibi_bi->isScrollDragging())
    {
        draw_bibi_bi->updateScrollDrag(pt);
        InvalidateRect(hwnd, NULL, FALSE);
        return;
    }

    TRACKMOUSEEVENT track = {};
    track.cbSize = sizeof(track);
    track.dwFlags = TME_LEAVE;
    track.hwndTrack = hwnd;
    TrackMouseEvent(&track);

    if (draw_bibi_bi->setHover(pt))
        InvalidateRect(hwnd, NULL, FALSE);
}

bool win_bi::OnSetCursor()
{
    if (!draw_bibi_bi)
        return false;

    POINT local;
    GetCursorPos(&local);
    ScreenToClient(hwnd, &local);

    float scale = dpi_bi::scaleForWindow(hwnd);
    if (scale > 0.01f)
    {
        local.x = (LONG)((float)local.x / scale + 0.5f);
        local.y = (LONG)((float)local.y / scale + 0.5f);
    }

    if (draw_bibi_bi->isOverInteractive(local))
    {
        SetCursor(LoadCursor(NULL, IDC_HAND));
        return true;
    }

    return false;
}

void win_bi::OnUpdateNotify()
{
    if (draw_bibi_bi->selectedTab == draw_batteryinfo_bi::ABOUT_ME)
        InvalidateRect(hwnd, NULL, FALSE);
}

void win_bi::OnMouseWheel(WPARAM wParam, LPARAM lParam)
{
    short delta = GET_WHEEL_DELTA_WPARAM(wParam);

    draw_bibi_bi->scrollBy(-delta * 0.4f);

    InvalidateRect(hwnd, NULL, TRUE);
}

void win_bi::OnLeftButtonUp(WPARAM wParam, LPARAM lParam)
{
    if (draw_bibi_bi->isScrollDragging())
    {
        draw_bibi_bi->endScrollDrag();
        ReleaseCapture();
        InvalidateRect(hwnd, NULL, FALSE);
    }
}

void win_bi::OnLeftButtonDown(WPARAM wParam, LPARAM lParam)
{
    POINT click = ClientToDip(lParam);

    if (draw_bibi_bi->beginScrollDrag(click))
    {
        if (draw_bibi_bi->isScrollDragging())
            SetCapture(hwnd);

        InvalidateRect(hwnd, NULL, FALSE);
        return;
    }

    draw_batteryinfo_bi::click_result_bi result =
        draw_bibi_bi->handleClick(click, ov_bi.get(), ru_bi.get(), bi_bi.get());

    if (!result.handled)
        return;

    if (result.toggledAdmin)
        ru_bi->toggleStartAsAdmin();
    else if (result.toggledAutostart)
        ru_bi->toggleStartWithWindows();

    if (result.needsBrushRebuild)
    {
        ID2D1HwndRenderTarget *pRT = initd2d1_bi->GetOrCreateRenderTarget(hwnd);
        if (pRT)
            draw_bibi_bi->updateBrushes(pRT);
    }

    if (result.action != draw_batteryinfo_bi::ACT_NONE)
        RunAction(result.action);

    if (result.needsSave)
        SaveSettings();

    if (ov_bi && ov_bi->show_on_screen_display)
        UpdateOverlayHud();

    InvalidateRect(hwnd, nullptr, TRUE);
}

void win_bi::RunAction(int action)
{
    switch (action)
    {
    case draw_batteryinfo_bi::ACT_RESTART_ADMIN:
        if (!ru_bi->start_As_Admin)
        {
            ru_bi->start_As_Admin = true;
            ru_bi->toggleStartAsAdmin();
            SaveSettings();
        }

        if (autostart_bi::runTask())
        {
            log_bi::write("restarting elevated through Task Scheduler");
            DestroyWindow(hwnd);
        }
        else
        {
            log_bi::write("elevated restart failed");
            MessageBoxA(hwnd,
                        "Could not restart with administrator rights.\n"
                        "Close Kestrel and start it with 'Run as administrator'.",
                        APP_NAME, MB_ICONWARNING | MB_OK);
        }
        break;

    case draw_batteryinfo_bi::ACT_OPEN_REPO:
        ShellExecuteA(hwnd, "open", APP_REPO_URL, NULL, NULL, SW_SHOWNORMAL);
        break;

    case draw_batteryinfo_bi::ACT_OPEN_ISSUES:
        ShellExecuteA(hwnd, "open", APP_ISSUES_URL, NULL, NULL, SW_SHOWNORMAL);
        break;

    case draw_batteryinfo_bi::ACT_OPEN_LICENCE:
        ShellExecuteA(hwnd, "open", APP_LICENCE_URL, NULL, NULL, SW_SHOWNORMAL);
        break;

    case draw_batteryinfo_bi::ACT_OPEN_LOG:
    {
        std::string path = paths_bi::inDataDir(APP_LOG_FILE);
        if (!path.empty())
            ShellExecuteA(hwnd, "open", path.c_str(), NULL, NULL, SW_SHOWNORMAL);
        break;
    }

    case draw_batteryinfo_bi::ACT_CHECK_UPDATE:
        updater->checkAsync(hwnd);
        break;

    case draw_batteryinfo_bi::ACT_DOWNLOAD_UPDATE:
        updater->downloadAsync(hwnd);
        break;

    case draw_batteryinfo_bi::ACT_INSTALL_UPDATE:
        SaveSettings();

        if (updater->applyAndRestart())
        {
            DestroyWindow(hwnd);
        }
        else
        {
            MessageBoxA(hwnd,
                        "Could not install the update.\n"
                        "Kestrel could not replace its own file. If it lives in a\n"
                        "protected folder, move it somewhere writable and try again.",
                        APP_NAME, MB_ICONWARNING | MB_OK);
        }
        break;

    case draw_batteryinfo_bi::ACT_ROLLBACK:
        SaveSettings();

        if (updater->rollback())
        {
            DestroyWindow(hwnd);
        }
        else
        {
            MessageBoxA(hwnd, "Could not restore the previous version.",
                        APP_NAME, MB_ICONWARNING | MB_OK);
        }
        break;

    default:
        break;
    }
}

draw_batteryinfo_bi::diag_bi win_bi::BuildDiagnostics() const
{
    draw_batteryinfo_bi::diag_bi diag;

    if (etwTrace)
    {
        diag.frameTiming = etwTrace->running() &&
                           ov_bi && ov_bi->hud.metrics[HUD_M_FPS].available;

        if (!etwTrace->running())
            diag.frameReason = autostart_bi::isElevated() ? "Trace session refused"
                                                          : "Needs administrator";
        else if (!diag.frameTiming)
            diag.frameReason = "No frames from the foreground app";
    }
    else
    {
        diag.frameReason = "Not started";
    }

    if (ru_bi)
    {
        diag.cpuPower = ru_bi->cpuInfo.packagePowerAvailable;
        diag.gpuName = ru_bi->gpuInfo.gpuName;
        diag.threads = (int)ru_bi->cpuInfo.CoreUsagePercents.size();
    }

    if (bi_bi)
    {
        diag.battery = bi_bi->present;
        diag.chemistry = bi_bi->info_static.Chemistry;
    }

    return diag;
}

void win_bi::OnRightButtonDown(WPARAM wParam, LPARAM lParam)
{
}

void win_bi::OnTimer(WPARAM wParam)
{
    if (!bi_bi)
        return;

    switch (wParam)
    {
    case 1:
        bi_bi->QueryBatteryInfo();
        bi_bi->QueryBatteryStatus();
        UpdateTrayTooltip();

        if (IsWindowVisible(hwnd))
            InvalidateRect(hwnd, NULL, true);
        break;

    case 2:
        bi_bi->QueryBatteryRemaining();
        if (IsWindowVisible(hwnd))
            InvalidateRect(hwnd, NULL, true);
        break;

    case 3:
        if (ov_bi && ov_bi->show_on_screen_display)
            UpdateOverlayHud();
        break;
    }
}

void win_bi::OnSetFocus(HWND hwnd)
{
}

void win_bi::OnKillFocus(HWND hwnd)
{
}

void win_bi::OnSysCommand(WPARAM wParam, LPARAM lParam)
{
    DefWindowProc(hwnd, WM_SYSCOMMAND, wParam, lParam);
}

void win_bi::OnChar(WPARAM wParam)
{
}

void win_bi::OnGetMinMaxInfo(LPARAM lParam)
{
    float scale = (float)dpi_bi::forWindow(hwnd) / 96.0f;

    RECT frame = {0, 0,
                  (LONG)(WINDOW_DIP_W * scale + 0.5f),
                  (LONG)(WINDOW_DIP_H * scale + 0.5f)};
    AdjustWindowRectEx(&frame, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_CLIENTEDGE);

    MINMAXINFO *mmi = (MINMAXINFO *)lParam;
    mmi->ptMinTrackSize.x = frame.right - frame.left;
    mmi->ptMinTrackSize.y = frame.bottom - frame.top;
}

void win_bi::SaveSettings()
{
    settings.collectFrom(ru_bi.get(), ov_bi.get(), draw_bibi_bi.get(), bi_bi.get());
    settings.save();
}

void win_bi::OnClose()
{
    if (ru_bi && ru_bi->minimize_To_Tray)
    {
        ShowWindow(hwnd, SW_HIDE);
        AddTrayIcon();
    }
    else
        DestroyWindow(hwnd);
}

void win_bi::OnDestroy()
{
    if (destroyed)
        return;

    destroyed = true;

    KillTimer(hwnd, 1);
    KillTimer(hwnd, 2);
    KillTimer(hwnd, 3);
    UnregisterHotKey(hwnd, HOTKEY_TOGGLE_HUD);
    RemoveTrayIcon();

    SaveSettings();

    if (updater)
        updater->cancel();

    updater.reset();
    etwTrace.reset();
    ov_bi.reset();

    if (initdwrite_bi)
        initdwrite_bi->CleanupDirectWrite();

    if (ru_bi)
        ru_bi->cleanup();

    draw_bibi_bi.reset();
    initd2d1_bi.reset();
    ru_bi.reset();
    bi_bi.reset();

    log_bi::shutdown();

    PostQuitMessage(0);
}

static void focusExistingInstance()
{
    HWND existing = FindWindowA(APP_WINDOW_CLASS, NULL);
    if (!existing)
        return;

    ShowWindow(existing, SW_RESTORE);
    SetForegroundWindow(existing);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    log_bi::init();

    int exitCode = 0;
    if (autostart_bi::handleCommandLine(lpCmdLine, &exitCode))
    {
        log_bi::shutdown();
        return exitCode;
    }

    bool fromTask = lpCmdLine && strstr(lpCmdLine, autostart_bi::ARG_FROM_TASK) != NULL;

    if (!fromTask && !autostart_bi::isElevated() && autostart_bi::taskExists())
    {
        log_bi::write("not elevated but task exists, relaunching through Task Scheduler");
        if (autostart_bi::runTask())
        {
            log_bi::shutdown();
            return 0;
        }
        log_bi::write("relaunch failed, continuing without elevation");
    }

    HANDLE instanceMutex = CreateMutexA(NULL, TRUE, APP_MUTEX_NAME);
    if (instanceMutex && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        log_bi::write("another instance is already running, focusing it");
        focusExistingInstance();
        CloseHandle(instanceMutex);
        log_bi::shutdown();
        return 0;
    }

    int result = 0;
    {
        win_bi mainWindow(hInstance);
        if (mainWindow.Register() && mainWindow.Create(nCmdShow))
        {
            result = (int)mainWindow.RunMessageLoop();
        }
    }

    if (instanceMutex)
    {
        ReleaseMutex(instanceMutex);
        CloseHandle(instanceMutex);
    }

    log_bi::shutdown();
    return result;
}
