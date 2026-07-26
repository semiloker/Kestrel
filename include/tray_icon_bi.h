#ifndef TRAY_ICON_BI_H
#define TRAY_ICON_BI_H

#include <windows.h>
#include <string>

class tray_icon_bi
{
public:
    tray_icon_bi();
    ~tray_icon_bi();

    bool add(HWND hwnd, HICON hIcon);
    void remove();
    void updateTooltip(const std::string &text);

    // Re-adds the icon after explorer restarted, but only if it was supposed to
    // be on screen. No-op otherwise.
    bool restore();

    bool visible() const { return visible_; }

    // Explorer broadcasts this after a restart; every shell icon has to be
    // re-added when it arrives.
    static UINT taskbarCreatedMessage();

private:
    NOTIFYICONDATA nid;
    HWND owner_ = NULL;
    HICON icon_ = NULL;
    bool visible_ = false;
    bool wanted_ = false;
};

#endif
