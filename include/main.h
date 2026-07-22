#ifndef MAIN_H
#define MAIN_H

#include <minwindef.h>
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <memory>
#include <string>
#include <vector>

#include "BatteryInfo.h"
#include "init_d2d1_bi.h"
#include "init_dwrite_bi.h"
#include "draw_batteryinfo_bi.h"
#include "overlay_bi.h"
#include "resource_usage_bi.h"
#include "capture_bi.h"
#include "etw_bi.h"
#include "frame_stats_bi.h"
#include "settings_bi.h"
#include "update_bi.h"

class win_bi
{
public:
    win_bi(HINSTANCE hInstance);
    ~win_bi() = default;

    bool Register();
    bool Create(int nCmdShow);

    void AddTrayIcon();
    void UpdateTrayTooltip();
    void RemoveTrayIcon();
    void ShowTrayMenu();

    void UpdateOverlayHud();

    WPARAM RunMessageLoop();

    std::unique_ptr<resource_usage_bi> ru_bi;

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
    void SaveSettings();
    void RunAction(int action);
    draw_batteryinfo_bi::diag_bi BuildDiagnostics();

    static const char szClassName[];

    NOTIFYICONDATA nid;
    bool trayIconVisible = false;

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

    settings_bi settings;

    DWORD hudTargetPid = 0;

    DWORD hudApiPid = 0;
    const char *hudApiName = "-";

    DWORD lastFrameDataTick = 0;

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
    void ToggleCapture();
    draw_batteryinfo_bi::capture_view_bi BuildCaptureView();

    std::string cachedBackupVersion;
    bool backupVersionLoaded = false;

    capture_bi capture;
    capture_bi::summary_bi lastCapture;
    bool haveLastCapture = false;
    std::vector<capture_bi::summary_bi> captureHistory;
    bool captureHistoryLoaded = false;
};

#endif
