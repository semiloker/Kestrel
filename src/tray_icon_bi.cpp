#include "tray_icon_bi.h"
#include "logger_bi.h"

#include <cstring>

tray_icon_bi::tray_icon_bi()
{
    ZeroMemory(&nid, sizeof(NOTIFYICONDATA));
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_USER + 1;
}

tray_icon_bi::~tray_icon_bi()
{
    if (visible_)
        remove();
}

void tray_icon_bi::add(HWND hwnd, HICON hIcon)
{
    if (visible_)
        return;

    nid.hWnd = hwnd;
    nid.hIcon = hIcon;

    if (Shell_NotifyIcon(NIM_ADD, &nid))
    {
        visible_ = true;
        initialized_ = true;
    }
    else
    {
        log_bi::writeErr(GetLastError(), "tray: Shell_NotifyIcon(NIM_ADD) failed");
    }
}

void tray_icon_bi::remove()
{
    if (!visible_)
        return;

    Shell_NotifyIcon(NIM_DELETE, &nid);
    visible_ = false;
}

void tray_icon_bi::updateTooltip(const std::string &text)
{
    if (!visible_)
        return;

    strncpy_s(nid.szTip, text.c_str(), sizeof(nid.szTip) - 1);
    nid.uFlags = NIF_TIP;
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}
