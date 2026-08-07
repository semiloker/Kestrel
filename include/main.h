#ifndef MAIN_H
#define MAIN_H

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <memory>
#include <string>
#include <vector>

#include "BatteryInfo.h"
#include "battery_history_bi.h"
#include "init_d2d1_bi.h"
#include "init_dwrite_bi.h"
#include "draw_batteryinfo_bi.h"
#include "overlay_bi.h"
#include "resource_usage_bi.h"
#include "capture_bi.h"
#include "capture_manager_bi.h"
#include "etw_bi.h"
#include "frame_stats_bi.h"
#include "hotkey_manager_bi.h"
#include "settings_bi.h"
#include "tray_icon_bi.h"
#include "update_bi.h"

class win_bi
{
public:
    enum restart_request_bi
    {
        RESTART_NONE = 0,
        RESTART_UPDATED,
        RESTART_ROLLED_BACK
    };

    win_bi(HINSTANCE hInstance);
    ~win_bi();

    bool Register();
    bool Create(int nCmdShow, bool startInTray);

    bool AddTrayIcon();
    void UpdateTrayTooltip();
    void RemoveTrayIcon();
    void ShowTrayMenu();
    void OnTaskbarCreated();

    void UpdateOverlayHud();

    WPARAM RunMessageLoop();

    std::unique_ptr<resource_usage_bi> ru_bi;

    restart_request_bi restartRequest() const
    {
        return restartMode;
    }
    HANDLE takeRestartGuard();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void OnGetMinMaxInfo(LPARAM lParam);

    void OnCommand(WPARAM wParam);
    void OnCreate(HWND hwnd);
    void OnResize(WPARAM wParam);
    void OnPaint(HWND hwnd);
    void OnKeyDown(WPARAM wParam);
    void OnKeyUp(WPARAM wParam);
    void OnMouseMove(WPARAM wParam, LPARAM lParam);
    void OnMouseWheel(WPARAM wParam, LPARAM lParam);
    void OnLeftButtonDown(WPARAM wParam, LPARAM lParam);
    void OnLeftButtonUp(WPARAM wParam, LPARAM lParam);
    void OnRightButtonDown(WPARAM wParam, LPARAM lParam);
    bool OnSetCursor();
    void OnUpdateNotify();
    POINT ClientToDip(LPARAM lParam) const;
    void OnTimer(WPARAM wParam);
    void OnSetFocus(HWND hwnd);
    void OnKillFocus(HWND hwnd);
    void OnSysCommand(WPARAM wParam, LPARAM lParam);
    void OnChar(WPARAM wParam);
    void OnClose();
    void OnDestroy();

    void OnDpiChanged(WPARAM wParam, LPARAM lParam);
    void OnHotKey(WPARAM wParam);

    void ToggleOverlay();
    void ToggleCapture();
    void SaveSettings();
    void RunStartupWizard();  // UX-08: guided first-run
    void RunAction(int action);
    draw_batteryinfo_bi::diag_bi BuildDiagnostics();

    HotkeyManager hotkeys;
    capture_manager_bi captureMgr;
    tray_icon_bi trayIcon;

    static const char szClassName[];

    HINSTANCE hInstance;
    HWND hwnd;

    HICON hAppIcon = NULL;
    HICON hAppIconSmall = NULL;

    POINT pt;

    bool isMinimized = false;

    bool destroyed = false;

    std::unique_ptr<batteryinfo_bi> bi_bi;
    std::unique_ptr<init_d2d1_bi> initd2d1_bi;
    std::unique_ptr<init_dwrite_bi> initdwrite_bi;
    std::unique_ptr<draw_batteryinfo_bi> draw_bibi_bi;
    std::unique_ptr<overlay_bi> ov_bi;
    std::unique_ptr<etw_bi> etwTrace;
    std::unique_ptr<update_bi> updater;
    restart_request_bi restartMode = RESTART_NONE;
    HANDLE restartGuard = INVALID_HANDLE_VALUE;

    settings_bi settings;

    battery_history_bi batteryHistory;

    DWORD hudTargetPid = 0;
    DWORD lastProfilePid = 0;
    std::string currentProfileExe;

    DWORD hudApiPid = 0;
    const char *hudApiName = "-";

    DWORD lastFrameDataTick = 0;
    DWORD lastEtwRestartTick = 0;
    bool firstRun_ = false;

    unsigned hudTick = 0;
    double lastGpuMsPerFrame = 0.0;
    bool haveGpuMs = false;

    resource_usage_bi::CpuInfo snapCpu;
    resource_usage_bi::RamInfo snapRam;
    resource_usage_bi::GpuInfo snapGpu;
    double snapGpuBusyMs = 0.0;
    bool snapGpuBusyValid = false;

    frame_stats_bi frameStats;
    std::vector<etw_bi::frame_sample_bi> frameScratch;

    void collectFrames();
    void UpdateDerivedMetrics();
    draw_batteryinfo_bi::capture_view_bi BuildCaptureView();

    std::string cachedBackupVersion;
    bool backupVersionLoaded = false;
};

#endif
