#ifndef TRAY_ICON_BI_H
#define TRAY_ICON_BI_H

#include <windows.h>
#include <string>

class tray_icon_bi
{
public:
    tray_icon_bi();
    ~tray_icon_bi();

    void add(HWND hwnd, HICON hIcon);
    void remove();
    void updateTooltip(const std::string &text);
    bool visible() const { return visible_; }

private:
    NOTIFYICONDATA nid;
    bool visible_ = false;
    bool initialized_ = false;
};

#endif
