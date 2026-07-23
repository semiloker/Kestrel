#include "main.h"
#include "resource_bi.h"
#include "autostart_bi.h"
#include "logger_bi.h"
#include "paths_bi.h"
#include "dpi_bi.h"
#include "app_identity_bi.h"
#include "settings_bi.h"
#include "i18n_bi.h"

#include <commctrl.h>
#include <cstring>
#include <cmath>
#include <format>

const char win_bi::szClassName[] = APP_WINDOW_CLASS;

#define WINDOW_DIP_W 550
#define WINDOW_DIP_H 750

win_bi::win_bi(HINSTANCE hInstance) : hInstance(hInstance), hwnd(NULL)
{
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

bool win_bi::Create(int nCmdShow, bool startInTray)
{
    bi_bi.reset(new batteryinfo_bi());
    initd2d1_bi.reset(new init_d2d1_bi());
    initdwrite_bi.reset(new init_dwrite_bi());
    draw_bibi_bi.reset(new draw_batteryinfo_bi());
    draw_bibi_bi->setDWriteFactory(initdwrite_bi->pDWriteFactory);
    ru_bi.reset(new resource_usage_bi());
    ov_bi.reset(new overlay_bi());

    ov_bi->margin = initdwrite_bi->overlay_pos_x;
    ov_bi->hud.initStaticInfo(ru_bi->gpuInfo.gpuName);

    // First run == no settings file yet (load() does not create one).
    std::string settingsPath = paths_bi::inDataDir("settings.ini");
    firstRun_ = !settingsPath.empty() &&
                GetFileAttributesA(settingsPath.c_str()) == INVALID_FILE_ATTRIBUTES;

    if (auto r = settings.load(); !r.ok())
        log_bi::write("settings: %s", r.error().c_str());
    settings.applyTo(ru_bi.get(), ov_bi.get(), draw_bibi_bi.get(), bi_bi.get());

    i18n_bi::load(settings.getString("ui.language", "en"));

    // Probe temperature sensors once so the temp toggles reflect real
    // availability. Without this the toggle is un-clickable (availability is
    // only set while temps are shown, which needs the toggle already on).
    ru_bi->updateTemps();

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

    if (startInTray && ru_bi && ru_bi->minimize_To_Tray)
    {
        AddTrayIcon();
        log_bi::write("autostart: launched into the tray");
    }
    else
    {
        ShowWindow(hwnd, nCmdShow);
        UpdateWindow(hwnd);
    }

    if (firstRun_)
        RunStartupWizard();

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
        else if (draw_bibi_bi->selectedTab == draw_batteryinfo_bi::CAPTURE)
        {
            draw_bibi_bi->drawCaptureTab(pRenderTarget, initdwrite_bi.get(),
                                         BuildCaptureView());
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

            if (view.backupAvailable && !backupVersionLoaded)
            {
                cachedBackupVersion = updater->backupVersion();
                backupVersionLoaded = true;
            }

            view.backupVersion = view.backupAvailable ? cachedBackupVersion : std::string();

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

    if (ov_bi && ov_bi->show_on_screen_display)
    {
        ov_bi->setScale(ov_bi->getScale());
        ov_bi->UpdatePosition();
    }

    log_bi::write("dpi changed to %u", (unsigned)HIWORD(wParam));
}

void win_bi::OnHotKey(WPARAM wParam)
{
    HotkeyManager::Action a = hotkeys.identify(wParam);

    switch (a)
    {
    case HotkeyManager::ACTION_TOGGLE_HUD:
        ToggleOverlay();
        break;

    case HotkeyManager::ACTION_PRESET_MINIMAL:
    case HotkeyManager::ACTION_PRESET_GAMING:
    case HotkeyManager::ACTION_PRESET_EVERYTHING:
        draw_bibi_bi->applyPresetExternal(hotkeys.presetIndex(a),
                                          ov_bi.get(), ru_bi.get(), bi_bi.get());
        SaveSettings();
        UpdateOverlayHud();
        InvalidateRect(hwnd, NULL, TRUE);
        break;

    case HotkeyManager::ACTION_CAPTURE:
        captureMgr.toggle(etw_bi::processName(hudTargetPid));
        draw_bibi_bi->setTab(draw_batteryinfo_bi::CAPTURE);
        if (ov_bi && ov_bi->show_on_screen_display)
            UpdateOverlayHud();
        InvalidateRect(hwnd, NULL, TRUE);
        break;

    default:
        break;
    }
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
    trayIcon.add(hwnd, hAppIcon);
    UpdateTrayTooltip();
}

void win_bi::UpdateTrayTooltip()
{
    if (!trayIcon.visible() || !bi_bi)
        return;

    std::string tooltip =
        "Power State: " + bi_bi->info_1s.PowerState + "\n" +
        "Charge: " + bi_bi->info_1s.ChargeLevel + "\n" +
        "Voltage: " + bi_bi->info_1s.Voltage;

    trayIcon.updateTooltip(tooltip);
}

void win_bi::RemoveTrayIcon()
{
    trayIcon.remove();
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

    SetTimer(hwnd, 3, (UINT)(ov_bi ? ov_bi->refreshMs : HUD_SAMPLE_INTERVAL_MS), NULL);

    hotkeys.registerAll(hwnd);

    bool batteryOk = bi_bi->Initialize();
    ru_bi->updateRam();

    ru_bi->startSampler(250);

    if (!batteryOk)
        log_bi::write("no battery present, or the driver refused the IOCTL");

    draw_bibi_bi->processElevated = autostart_bi::isElevated();

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

void win_bi::collectFrames()
{
    if (!etwTrace || !ov_bi)
        return;

    if (frameScratch.size() < 4096)
        frameScratch.resize(4096);

    size_t taken = 0;

    do
    {
        taken = etwTrace->drainFrames(&frameScratch[0], frameScratch.size());

        for (size_t i = 0; i < taken; ++i)
        {
            frameStats.push(frameScratch[i].intervalMs, frameScratch[i].time100ns);
            captureMgr.getCapture().addFrame(frameScratch[i].intervalMs, frameScratch[i].time100ns);
        }
    } while (taken == frameScratch.size());

    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    LONGLONG now = ((LONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;

    frameStats.trimToWindow(now, 20.0);

    ov_bi->hud.low1Valid = frameStats.hasEnoughFor(0.01);
    ov_bi->hud.low01Valid = frameStats.hasEnoughFor(0.001);
    ov_bi->hud.low1PercentFps = frameStats.lowFps(0.01);
    ov_bi->hud.low01PercentFps = frameStats.lowFps(0.001);
}

void win_bi::UpdateDerivedMetrics()
{
    if (!ov_bi || !ru_bi || !bi_bi)
        return;

    hud_bi &hud = ov_bi->hud;

    double fps = hud.metrics[HUD_M_FPS].series.current();
    bool fpsUsable = hud.metrics[HUD_M_FPS].available && fps > 0.5;

    hud.efficiencyValid = false;
    if (fpsUsable && snapCpu.packagePowerAvailable && snapCpu.packagePowerW > 0.5)
    {
        hud.efficiencyFpsPerWatt = fps / snapCpu.packagePowerW;
        hud.efficiencyValid = true;
    }

    hud.bottleneck = hud_bi::BOUND_UNKNOWN;
    hud.bottleneckRatio = 0.0;

    double frameMs = frameStats.empty() ? 0.0 : frameStats.averageMs();

    if (fpsUsable && haveGpuMs && frameMs > 0.1 && lastGpuMsPerFrame > 0.0)
    {
        double ratio = lastGpuMsPerFrame / frameMs;
        if (ratio > 1.5)
            ratio = 1.5;

        hud.bottleneckRatio = ratio;

        if (ratio >= 0.95)
            hud.bottleneck = hud_bi::BOUND_GPU;
        else if (ratio < 0.70)
            hud.bottleneck = hud_bi::BOUND_CPU;
        else
            hud.bottleneck = hud_bi::BOUND_MIXED;
    }

    hud.chargerDeficit = false;
    hud.chargerDeficitW = 0.0;

    if (bi_bi->present && bi_bi->info_1s.onLine && bi_bi->info_1s.discharging &&
        bi_bi->info_1s.rateValid && bi_bi->info_1s.rateW < -0.5)
    {
        hud.chargerDeficit = true;
        hud.chargerDeficitW = bi_bi->info_1s.rateW;
    }

    hud.capturing = captureMgr.getCapture().active();
    hud.captureSeconds = hud.capturing ? captureMgr.getCapture().elapsedSeconds() : 0.0;
    hud.captureFrames = captureMgr.getCapture().frameCount();

    if (captureMgr.getCapture().active())
    {
        captureMgr.getCapture().addPowerSample(snapCpu.packagePowerW,
                               snapCpu.packagePowerAvailable,
                               bi_bi->info_1s.chargePercent,
                               bi_bi->present && bi_bi->info_1s.chargeValid,
                               bi_bi->info_1s.rateW,
                               bi_bi->present && bi_bi->info_1s.rateValid,
                               bi_bi->info_static.capacityValid
                                   ? bi_bi->info_static.fullChargedWh
                                   : 0.0);
    }
}
draw_batteryinfo_bi::capture_view_bi win_bi::BuildCaptureView()
{
    draw_batteryinfo_bi::capture_view_bi view;

    view.recording = captureMgr.getCapture().active();
    view.seconds = captureMgr.getCapture().elapsedSeconds();
    view.frames = captureMgr.getCapture().frameCount();
    view.liveFps = captureMgr.getCapture().liveAverageFps();
    view.liveLow1 = captureMgr.getCapture().liveLow1Fps();
    view.liveLow1Valid = captureMgr.getCapture().liveLow1Valid();

    view.hasLast = captureMgr.hasLastSummary();
    view.last = captureMgr.lastSummary();

    captureMgr.loadHistory();
    view.history = captureMgr.history();

    return view;
}

void win_bi::UpdateOverlayHud()
{
    if (!ov_bi)
        return;

    ++hudTick;

    std::vector<resource_usage_bi::AdapterInfo> snapAdapters;
    ru_bi->readSnapshot(&snapCpu, &snapRam, &snapGpu, &snapGpuBusyMs, &snapGpuBusyValid, &snapAdapters);

    HWND foreground = GetForegroundWindow();
    DWORD fgPid = 0;
    if (foreground)
        GetWindowThreadProcessId(foreground, &fgPid);

    if (fgPid != 0 && fgPid != GetCurrentProcessId())
    {
        hudTargetPid = fgPid;
        ov_bi->hud.updateForeground(foreground);
    }

    if (hudTargetPid != lastProfilePid)
    {
        lastProfilePid = hudTargetPid;

        std::string exe = etw_bi::processName(hudTargetPid);
        if (exe == "?" || exe == "<access denied>" || exe == "<unknown>")
            exe.clear();

        SaveSettings();
        settings.setProfile(exe);
        settings.applyTo(ru_bi.get(), ov_bi.get(), draw_bibi_bi.get(), bi_bi.get());
        currentProfileExe = exe;
    }

    etw_bi::sample_bi s;

    if (etwTrace)
    {
        // ponytail: 3s cooldown so a persistently failing session doesn't
        // re-StartTrace every tick (spams logs, wastes cycles).
        if (etwTrace->hasFailed() &&
            (GetTickCount() - lastEtwRestartTick) > 3000)
        {
            lastEtwRestartTick = GetTickCount();
            log_bi::write("etw: restarting failed trace session");
            etwTrace->stop();
            if (etwTrace->start())
                log_bi::write("etw: auto-restarted successfully");
        }

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
            etw_bi::api_bi detected = etw_bi::detectApi(measuredPid);
            hudApiName = etw_bi::apiName(detected);
            etwTrace->autoConfigForApi(detected);
        }

        ov_bi->hud.setApi(hudApiName);
    }

    ru_bi->setSamplerTarget(measuredPid);

    double gpuBusyMs = snapGpuBusyMs;
    bool gpuTimeOk = snapGpuBusyValid;

    collectFrames();

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
                double window = (double)ru_bi->samplerInterval() / 1000.0;
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

    ov_bi->hud.metrics[HUD_M_CPUW].available = snapCpu.packagePowerAvailable;
    if (snapCpu.packagePowerAvailable)
        ov_bi->hud.push(HUD_M_CPUW, snapCpu.packagePowerW);

    ov_bi->hud.metrics[HUD_M_GPUW].available = snapGpu.gpuPowerAvailable;
    if (snapGpu.gpuPowerAvailable)
        ov_bi->hud.push(HUD_M_GPUW, snapGpu.gpuPowerW);

    ov_bi->hud.metrics[HUD_M_BATTERYD].available = bi_bi->present && bi_bi->info_1s.rateValid;
    if (bi_bi->present && bi_bi->info_1s.rateValid)
        ov_bi->hud.push(HUD_M_BATTERYD, fabs(bi_bi->info_1s.rateW));

    ov_bi->hud.push(HUD_M_CPU, snapCpu.UsageValue);
    ov_bi->hud.push(HUD_M_GPU, snapGpu.gpuLoadValue);
    ov_bi->hud.push(HUD_M_RAM, snapRam.loadValue);
    ov_bi->hud.push(HUD_M_COMMIT, snapRam.commitValue);

    ov_bi->hud.setMemory(snapRam.usedGB, snapRam.totalGB);

    ov_bi->hud.metrics[HUD_M_CPU].show = ru_bi->cpuInfo.show_UsagePercent;
    ov_bi->hud.metrics[HUD_M_GPU].show = ru_bi->gpuInfo.show_gpuLoad;
    ov_bi->hud.metrics[HUD_M_RAM].show = ru_bi->ramInfo.show_dwMemoryLoad;
    ov_bi->hud.metrics[HUD_M_COMMIT].show = ru_bi->ramInfo.show_ullTotalPageFile;
    ov_bi->hud.metrics[HUD_M_CPUW].show = ru_bi->cpuInfo.show_packagePower;
    ov_bi->hud.metrics[HUD_M_GPUW].show = ru_bi->gpuInfo.show_gpuPower;
    ov_bi->hud.showDevice = ru_bi->gpuInfo.show_gpuName;
    ov_bi->hud.showMem = ru_bi->ramInfo.show_ullTotalPhys;

    ov_bi->hud.showVram = ru_bi->gpuInfo.show_vram;
    ov_bi->hud.vramAvailable = snapGpu.vramAvailable;
    ov_bi->hud.vramUsedMB = snapGpu.vramUsedMB;
    ov_bi->hud.vramTotalMB = snapGpu.vramTotalMB;

    UpdateDerivedMetrics();

    ov_bi->hud.showCpuName = ru_bi->cpuInfo.show_cpuName;
    ov_bi->hud.showCpuArch = ru_bi->cpuInfo.show_architecture;
    ov_bi->hud.cpuName = snapCpu.cpuName;
    ov_bi->hud.cpuArch = snapCpu.architecture;

    // Slow sensors (network/disk/temps): their collectors were never invoked
    // anywhere, so these "done" rows always showed empty. Wire them here on a
    // ~1s cadence, gated by their own toggles. Main thread is COM MTA (see
    // BatteryInfo.cpp), so updateTemps' CoInitializeEx returns S_FALSE and
    // stays balanced. ponytail: 1s cadence, cache the WMI connection if temps
    // ever show up in a profiler.
    if ((GetTickCount() - lastSlowSensorTick) > 1000)
    {
        lastSlowSensorTick = GetTickCount();
        if (ov_bi->hud.showNetwork)
            ru_bi->updateNetwork();
        if (ov_bi->hud.showDisk)
            ru_bi->updateDisk();
        if (ru_bi->cpuInfo.show_cpuTemp || ru_bi->gpuInfo.show_gpuTemp)
            ru_bi->updateTemps();
    }

    // showNetwork/showDisk come from settings (hud.network / hud.disk); do NOT
    // rebind them to unrelated CPU/RAM toggles.
    if (ov_bi->hud.showNetwork)
    {
        if (!ru_bi->networkInfo.empty())
        {
            double totalDown = 0.0, totalUp = 0.0;
            for (size_t i = 0; i < ru_bi->networkInfo.size(); ++i)
            {
                const char *ds = ru_bi->networkInfo[i].downloadSpeed.c_str();
                const char *us = ru_bi->networkInfo[i].uploadSpeed.c_str();
                totalDown += atof(ds);
                totalUp += atof(us);
            }
            ov_bi->hud.netDownKBs = totalDown;
            ov_bi->hud.netUpKBs = totalUp;
            ov_bi->hud.push(HUD_M_NETDOWN, totalDown);
            ov_bi->hud.push(HUD_M_NETUP, totalUp);
            ov_bi->hud.metrics[HUD_M_NETDOWN].available = true;
            ov_bi->hud.metrics[HUD_M_NETUP].available = true;
        }
        else
        {
            ov_bi->hud.metrics[HUD_M_NETDOWN].available = false;
            ov_bi->hud.metrics[HUD_M_NETUP].available = false;
        }
    }

    if (ov_bi->hud.showDisk && !ru_bi->disksInfo.empty())
    {
        const resource_usage_bi::DiskInfo &disk = ru_bi->disksInfo[0];
        ov_bi->hud.diskAvailable = true;
        double totalMB = atof(disk.totalSpace.c_str());
        double usedMB = atof(disk.usedSpace.c_str());
        ov_bi->hud.diskTotalGB = totalMB / 1024.0;
        ov_bi->hud.diskUsedGB = usedMB / 1024.0;
        ov_bi->hud.push(HUD_M_DISK, usedMB / (totalMB > 0.0 ? totalMB : 1.0) * 100.0);
    }
    else
    {
        ov_bi->hud.diskAvailable = false;
    }

    ov_bi->hud.clearExtraRows();

    if (ru_bi->cpuInfo.show_CoreUsagePercents)
    {
        for (size_t i = 0; i < snapCpu.CoreUsagePercents.size(); ++i)
            ov_bi->hud.addExtraRow("Core " + std::to_string(i + 1),
                                   snapCpu.CoreUsagePercents[i]);
    }

    if (ru_bi->cpuInfo.show_cpuTemp && ru_bi->cpuInfo.cpuTempAvailable)
    {
        std::string val = std::format("{:.0f} C", ru_bi->cpuInfo.cpuTempC);
        ov_bi->hud.addExtraRow("CPU Temp", val);
    }

    if (ru_bi->gpuInfo.show_gpuTemp && ru_bi->gpuInfo.gpuTempAvailable)
    {
        std::string val = std::format("{:.0f} C", ru_bi->gpuInfo.gpuTempC);
        ov_bi->hud.addExtraRow("GPU Temp", val);
    }

    if (ru_bi->ramInfo.show_ullAvailPhys)
        ov_bi->hud.addExtraRow("Avail RAM", snapRam.ullAvailPhys);
    if (ru_bi->ramInfo.show_ullAvailPageFile)
        ov_bi->hud.addExtraRow("Avail Page", snapRam.ullAvailPageFile);
    if (ru_bi->ramInfo.show_ullTotalVirtual)
        ov_bi->hud.addExtraRow("Total Virt", snapRam.ullTotalVirtual);
    if (ru_bi->ramInfo.show_ullAvailVirtual)
        ov_bi->hud.addExtraRow("Avail Virt", snapRam.ullAvailVirtual);
    if (ru_bi->ramInfo.show_ullAvailExtendedVirtual)
        ov_bi->hud.addExtraRow("Ext Virt", snapRam.ullAvailExtendedVirtual);

    if (bi_bi->info_1s.Voltage_)
        ov_bi->hud.addExtraRow("Voltage", bi_bi->info_1s.Voltage);
    if (bi_bi->info_1s.Rate_)
        ov_bi->hud.addExtraRow("Rate", bi_bi->info_1s.Rate);
    if (bi_bi->info_1s.PowerState_)
        ov_bi->hud.addExtraRow("Power", bi_bi->info_1s.PowerState);
    if (bi_bi->info_1s.RemainingCapacity_)
        ov_bi->hud.addExtraRow("Remaining", bi_bi->info_1s.RemainingCapacity);
    if (bi_bi->info_1s.ChargeLevel_)
        ov_bi->hud.addExtraRow("Charge", bi_bi->info_1s.ChargeLevel);
    if (bi_bi->info_10s.TimeRemaining_)
        ov_bi->hud.addExtraRow("Time Left", bi_bi->info_10s.TimeRemaining);

    if (ru_bi->gpuInfo.show_adapters && snapAdapters.size() > 1)
    {
        for (size_t i = 0; i < snapAdapters.size(); ++i)
        {
            const auto &a = snapAdapters[i];
            if (!a.available)
                continue;

            bool useGb = (ru_bi->memUnit == resource_usage_bi::MEM_UNIT_GB) ||
                         (ru_bi->memUnit == resource_usage_bi::MEM_UNIT_AUTO &&
                          a.totalVramMB >= 1024.0);

            std::string label = "GPU " + std::to_string(i);
            if (!a.adapterName.empty())
                label += " " + a.adapterName;

            std::string row;
            if (useGb)
                row = std::format("{:.2f} GB / {:.2f} GB", a.usedVramMB / 1024.0, a.totalVramMB / 1024.0);
            else
                row = std::format("{:.0f} MB / {:.0f} MB", a.usedVramMB, a.totalVramMB);

            ov_bi->hud.addExtraRow(label, row);
        }
    }

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

    if (draw_bibi_bi->isGraphHeightDragging())
    {
        draw_bibi_bi->updateGraphHeightDrag(pt, ov_bi.get());
        if (ov_bi && ov_bi->show_on_screen_display)
            UpdateOverlayHud();
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

    if (draw_bibi_bi->isGraphHeightDragging())
    {
        draw_bibi_bi->endGraphHeightDrag();
        ReleaseCapture();
        SaveSettings();
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

    if (draw_bibi_bi->beginGraphHeightDrag(click, ov_bi.get()))
    {
        SetCapture(hwnd);
        if (ov_bi && ov_bi->show_on_screen_display)
            UpdateOverlayHud();
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

    if (result.refreshChanged)
    {
        KillTimer(hwnd, 3);
        SetTimer(hwnd, 3, (UINT)ov_bi->refreshMs, NULL);
        log_bi::write("overlay refresh set to %d ms", ov_bi->refreshMs);
    }

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

    case draw_batteryinfo_bi::ACT_TOGGLE_CAPTURE:
        captureMgr.toggle(etw_bi::processName(hudTargetPid));
        draw_bibi_bi->setTab(draw_batteryinfo_bi::CAPTURE);
        if (ov_bi && ov_bi->show_on_screen_display)
            UpdateOverlayHud();
        InvalidateRect(hwnd, NULL, TRUE);
        break;

    case draw_batteryinfo_bi::ACT_OPEN_CAPTURES:
    {
        std::string dir = capture_bi::capturesDir();
        if (!dir.empty())
            ShellExecuteA(hwnd, "open", dir.c_str(), NULL, NULL, SW_SHOWNORMAL);
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

    case draw_batteryinfo_bi::ACT_SAVE_PROFILE:
    {
        std::string exe = etw_bi::processName(hudTargetPid);
        if (exe != "?" && exe != "<access denied>" && exe != "<unknown>")
        {
            settings.saveProfile(exe, ru_bi.get(), ov_bi.get(), draw_bibi_bi.get(), bi_bi.get());
            if (auto r = settings.save(); !r.ok())
                log_bi::write("settings: %s", r.error().c_str());
            log_bi::write("profile: saved for '%s'", exe.c_str());
        }
        break;
    }

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

draw_batteryinfo_bi::diag_bi win_bi::BuildDiagnostics()
{
    draw_batteryinfo_bi::diag_bi diag;

    if (ru_bi)
    {
        std::vector<resource_usage_bi::AdapterInfo> diagAdapters;
        ru_bi->readSnapshot(&snapCpu, &snapRam, &snapGpu, &snapGpuBusyMs, &snapGpuBusyValid, &diagAdapters);
    }

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
        diag.cpuPower = snapCpu.packagePowerAvailable;
        diag.gpuName = ru_bi->gpuInfo.gpuName;
        diag.threads = (int)snapCpu.CoreUsagePercents.size();
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
        if (ov_bi)
        {
            if (ov_bi->autoHideOverlay)
            {
                HWND fg = GetForegroundWindow();
                bool gameRunning = false;

                // The desktop shell (Progman/WorkerW) and the taskbar are also
                // full-screen sized, so a naive size check treats "sitting on the
                // desktop" as a running game and un-hides the overlay. Exclude them.
                bool isShell = false;
                if (fg)
                {
                    if (fg == GetShellWindow())
                        isShell = true;
                    else
                    {
                        char cls[64] = {0};
                        GetClassNameA(fg, cls, sizeof(cls));
                        isShell = (strcmp(cls, "Progman") == 0 ||
                                   strcmp(cls, "WorkerW") == 0 ||
                                   strcmp(cls, "Shell_TrayWnd") == 0);
                    }
                }

                if (fg && !isShell)
                {
                    RECT wr;
                    if (GetWindowRect(fg, &wr))
                    {
                        int sw = GetSystemMetrics(SM_CXSCREEN);
                        int sh = GetSystemMetrics(SM_CYSCREEN);
                        gameRunning = (wr.right - wr.left) >= sw &&
                                      (wr.bottom - wr.top) >= sh;
                    }
                }

                bool wantShow = gameRunning && ov_bi->show_on_screen_display;

                if (wantShow && ov_bi->overlayAutoHidden)
                {
                    ov_bi->overlayAutoHidden = false;
                    ShowWindow(ov_bi->g_hwnd, SW_SHOWNOACTIVATE);
                }
                else if (!wantShow && !ov_bi->overlayAutoHidden && ov_bi->g_hwnd &&
                         IsWindowVisible(ov_bi->g_hwnd))
                {
                    ov_bi->overlayAutoHidden = true;
                    ShowWindow(ov_bi->g_hwnd, SW_HIDE);
                }
            }
            else if (ov_bi->overlayAutoHidden)
            {
                ov_bi->overlayAutoHidden = false;
                if (ov_bi->g_hwnd)
                    ShowWindow(ov_bi->g_hwnd, SW_SHOWNOACTIVATE);
                ov_bi->Render();
            }

            if (ov_bi->show_on_screen_display || captureMgr.getCapture().active())
                UpdateOverlayHud();
            else
                collectFrames();
        }
        else
        {
            collectFrames();
        }
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
    if (!draw_bibi_bi || !draw_bibi_bi->searchFocused)
        return;

    char c = (char)wParam;

    if (c == '\b')
    {
        if (!draw_bibi_bi->searchQuery.empty())
            draw_bibi_bi->searchQuery.pop_back();
    }
    else if (c == 27)
    {
        draw_bibi_bi->searchFocused = false;
        draw_bibi_bi->searchQuery.clear();
    }
    else if (c >= 32 && c < 127)
    {
        draw_bibi_bi->searchQuery += c;
    }

    InvalidateRect(hwnd, NULL, FALSE);
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
    if (auto r = settings.save(); !r.ok())
        log_bi::write("settings: %s", r.error().c_str());
}

// UX-08: guided first-run. Native TaskDialog (comctl32 v6 is in the manifest) —
// no custom D2D wizard needed. Picks overlay corner + autostart, and points the
// user at the admin setting that unlocks frame timing. Strings go through i18n.
void win_bi::RunStartupWizard()
{
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    const int ID_ADMIN = 101;   // autostart elevated (frame timing)
    const int ID_BASIC = 102;   // continue, no autostart

    TASKDIALOG_BUTTON links[2] = {
        {ID_ADMIN, i18n_bi::tr("wizard.admin",
            L"Enable frame timing\nStart with Windows as administrator so FPS and "
            L"frame-time metrics work (uses ETW).")},
        {ID_BASIC, i18n_bi::tr("wizard.basic",
            L"Continue in basic mode\nBattery, CPU, GPU and power work without "
            L"administrator. You can change this later in Settings.")},
    };

    TASKDIALOG_BUTTON corners[4] = {
        {200, i18n_bi::tr("corner.tl", L"Top-left")},
        {201, i18n_bi::tr("corner.tr", L"Top-right")},
        {202, i18n_bi::tr("corner.bl", L"Bottom-left")},
        {203, i18n_bi::tr("corner.br", L"Bottom-right")},
    };

    TASKDIALOGCONFIG cfg = {};
    cfg.cbSize = sizeof(cfg);
    cfg.hwndParent = hwnd;
    cfg.hInstance = hInstance;
    cfg.dwFlags = TDF_USE_COMMAND_LINKS | TDF_POSITION_RELATIVE_TO_WINDOW;
    cfg.pszWindowTitle = i18n_bi::tr("wizard.title", L"Kestrel Setup");
    cfg.pszMainIcon = TD_INFORMATION_ICON;
    cfg.pszMainInstruction = i18n_bi::tr("wizard.welcome", L"Welcome to Kestrel");
    cfg.pszContent = i18n_bi::tr("wizard.intro",
        L"An always-on-top overlay for battery, system load and frame timing. "
        L"Choose where it sits and how it starts.");
    cfg.pButtons = links;
    cfg.cButtons = 2;
    cfg.pRadioButtons = corners;
    cfg.cRadioButtons = 4;
    cfg.nDefaultRadioButton = 200 + (int)ov_bi->corner;
    cfg.pszVerificationText = i18n_bi::tr("wizard.autostart",
        L"Start Kestrel automatically with Windows");

    int button = 0, radio = 0;
    BOOL autostart = FALSE;
    if (TaskDialogIndirect(&cfg, &button, &radio, &autostart) != S_OK)
        return;  // dialog failed to show; skip silently, settings still get saved

    if (radio >= 200 && radio <= 203)
        ov_bi->corner = (overlay_bi::corner_bi)(radio - 200);

    bool wantAdmin = (button == ID_ADMIN);

    // Admin implies elevated autostart; the checkbox is honored otherwise.
    if (wantAdmin)
    {
        if (!ru_bi->isStartAsAdminEnabled())
            ru_bi->toggleStartAsAdmin();  // sets elevated autostart
    }
    else if (autostart)
    {
        if (ru_bi->isStartAsAdminEnabled())
            ru_bi->toggleStartAsAdmin();  // drop elevation, keep normal autostart
        if (!ru_bi->isStartWithWindowsEnabled())
            ru_bi->enableStartWithWindows();
    }
    else if (ru_bi->isStartWithWindowsEnabled())
    {
        ru_bi->disableStartWithWindows();
    }

    ov_bi->UpdatePosition();
    SaveSettings();  // creates settings.ini so the wizard won't reappear
    log_bi::write("wizard: first-run setup applied (corner=%d, admin=%d, autostart=%d)",
                  (int)ov_bi->corner, wantAdmin ? 1 : 0, autostart ? 1 : 0);
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
    hotkeys.unregisterAll(hwnd);
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
    {
        ru_bi->stopSampler();
        ru_bi->cleanup();
    }

    draw_bibi_bi.reset();
    initd2d1_bi.reset();
    ru_bi.reset();
    bi_bi.reset();

    log_bi::shutdown();

    PostQuitMessage(0);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
#ifdef _DEBUG
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    log_bi::init();

    int exitCode = 0;
    if (autostart_bi::handleCommandLine(lpCmdLine, &exitCode))
    {
        log_bi::shutdown();
        return exitCode;
    }

    // Handle --export-settings <path> and --import-settings <path>
    if (lpCmdLine)
    {
        std::string cmd(lpCmdLine);
        size_t expPos = cmd.find("--export-settings ");
        size_t impPos = cmd.find("--import-settings ");

        if (expPos != std::string::npos)
        {
            std::string path = cmd.substr(expPos + 18);
            size_t sp = path.find_first_not_of(" \t");
            if (sp != std::string::npos)
                path = path.substr(sp);
            size_t ep = path.find_first_of(" \t\"");
            if (ep != std::string::npos)
                path = path.substr(0, ep);

            settings_bi s;
            if (auto r = s.load(); !r.ok())
            {
                log_bi::write("settings: %s", r.error().c_str());
                log_bi::shutdown();
                return 1;
            }
            bool ok = s.exportJson(path.c_str());
            log_bi::write("settings export to %s: %s", path.c_str(), ok ? "OK" : "FAILED");
            log_bi::shutdown();
            return ok ? 0 : 1;
        }

        if (impPos != std::string::npos)
        {
            std::string path = cmd.substr(impPos + 18);
            size_t sp = path.find_first_not_of(" \t");
            if (sp != std::string::npos)
                path = path.substr(sp);
            size_t ep = path.find_first_of(" \t\"");
            if (ep != std::string::npos)
                path = path.substr(0, ep);

            settings_bi s;
            bool ok = s.importJson(path.c_str());
            if (ok)
            {
                auto r = s.save();
                ok = r.ok();
                if (!ok)
                    log_bi::write("settings: %s", r.error().c_str());
            }
            log_bi::write("settings import from %s: %s", path.c_str(), ok ? "OK" : "FAILED");
            log_bi::shutdown();
            return ok ? 0 : 1;
        }
    }

    bool fromTask = lpCmdLine && strstr(lpCmdLine, autostart_bi::ARG_FROM_TASK) != NULL;
    bool autostarted = fromTask ||
                       (lpCmdLine && strstr(lpCmdLine, autostart_bi::ARG_AUTOSTART) != NULL);

    if (!fromTask && !autostart_bi::isElevated() && autostart_bi::taskPointsToThisExe())
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
        HWND existing = FindWindowA(APP_WINDOW_CLASS, NULL);
        if (existing)
        {
            log_bi::write("another instance is already running, focusing it");
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
            CloseHandle(instanceMutex);
            log_bi::shutdown();
            return 0;
        }
        log_bi::write("orphaned mutex from crashed instance, taking over");
        DWORD waitResult = WaitForSingleObject(instanceMutex, 0);
        if (waitResult != WAIT_ABANDONED && waitResult != WAIT_OBJECT_0)
        {
            CloseHandle(instanceMutex);
            instanceMutex = CreateMutexA(NULL, TRUE, APP_MUTEX_NAME);
        }
    }

    int result = 0;
    {
        win_bi mainWindow(hInstance);
        if (mainWindow.Register() && mainWindow.Create(nCmdShow, autostarted))
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
