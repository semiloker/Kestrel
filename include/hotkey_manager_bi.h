#ifndef HOTKEY_MANAGER_BI_H
#define HOTKEY_MANAGER_BI_H

#include <windows.h>
#include <string>

class HotkeyManager
{
public:
    enum Action
    {
        ACTION_TOGGLE_HUD = 1,
        ACTION_PRESET_MINIMAL,
        ACTION_PRESET_GAMING,
        ACTION_PRESET_EVERYTHING,
        ACTION_CAPTURE,
        ACTION_NONE = 0
    };

    bool registerAll(HWND hwnd);
    void unregisterAll(HWND hwnd);
    Action identify(WPARAM wParam) const;
    int presetIndex(Action a) const;
    bool allRegistered() const { return failedMask == 0; }
    std::string conflictSummary() const;

private:
    unsigned registeredMask = 0;
    unsigned failedMask = 0;
};

#endif
