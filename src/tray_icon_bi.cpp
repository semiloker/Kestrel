#include "tray_icon_bi.h"
#include "logger_bi.h"

#include <cstring>

static const UINT TRAY_CALLBACK_MESSAGE = WM_USER + 1;
static const UINT TRAY_ICON_FLAGS = NIF_ICON | NIF_MESSAGE | NIF_TIP;

tray_icon_bi::tray_icon_bi()
{
    ZeroMemory(&nid, sizeof(NOTIFYICONDATA));
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.uID = 1;
    nid.uFlags = TRAY_ICON_FLAGS;
    nid.uCallbackMessage = TRAY_CALLBACK_MESSAGE;
}

tray_icon_bi::~tray_icon_bi()
{
    if (visible_)
        remove();
}

UINT tray_icon_bi::taskbarCreatedMessage()
{
    static const UINT msg = RegisterWindowMessage("TaskbarCreated");
    return msg;
}

bool tray_icon_bi::add(HWND hwnd, HICON hIcon)
{
    if (visible_)
        return true;

    owner_ = hwnd;
    icon_ = hIcon;

    nid.hWnd = hwnd;
    nid.hIcon = hIcon;

    // Restate the whole set on every add: NIM_ADD only honours what uFlags
    // advertises, so an icon added without NIF_ICON | NIF_MESSAGE lands in the
    // tray with no image and no click callback - invisible and unclickable.
    nid.uFlags = TRAY_ICON_FLAGS;
    nid.uCallbackMessage = TRAY_CALLBACK_MESSAGE;

    if (!Shell_NotifyIcon(NIM_ADD, &nid))
    {
        log_bi::writeErr(GetLastError(), "tray: Shell_NotifyIcon(NIM_ADD) failed");
        return false;
    }

    visible_ = true;
    wanted_ = true;
    return true;
}

void tray_icon_bi::remove()
{
    wanted_ = false;

    if (!visible_)
        return;

    Shell_NotifyIcon(NIM_DELETE, &nid);
    visible_ = false;
}

bool tray_icon_bi::restore()
{
    if (!wanted_ || !owner_)
        return false;

    // The previous explorer took the icon with it; drop our stale bookkeeping
    // so add() actually issues a fresh NIM_ADD.
    visible_ = false;
    return add(owner_, icon_);
}

void tray_icon_bi::updateTooltip(const std::string &text)
{
    strncpy_s(nid.szTip, text.c_str(), sizeof(nid.szTip) - 1);

    if (!visible_)
        return;

    // Modify through a copy: narrowing the stored uFlags to NIF_TIP would
    // corrupt the next NIM_ADD.
    NOTIFYICONDATA mod = nid;
    mod.uFlags = NIF_TIP;
    Shell_NotifyIcon(NIM_MODIFY, &mod);
}
