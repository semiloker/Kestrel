#include "hotkey_manager_bi.h"
#include "logger_bi.h"

bool HotkeyManager::registerAll(HWND hwnd)
{
    bool ok = true;

    if (!RegisterHotKey(hwnd, ACTION_TOGGLE_HUD, MOD_CONTROL | MOD_ALT, 'H'))
    {
        log_bi::writeErr(GetLastError(), "hotkey: Ctrl+Alt+H already taken by another app");
        ok = false;
    }

    RegisterHotKey(hwnd, ACTION_PRESET_MINIMAL, MOD_CONTROL | MOD_ALT, '1');
    RegisterHotKey(hwnd, ACTION_PRESET_GAMING, MOD_CONTROL | MOD_ALT, '2');
    RegisterHotKey(hwnd, ACTION_PRESET_EVERYTHING, MOD_CONTROL | MOD_ALT, '3');

    if (!RegisterHotKey(hwnd, ACTION_CAPTURE, MOD_CONTROL | MOD_ALT, 'B'))
    {
        log_bi::writeErr(GetLastError(), "hotkey: Ctrl+Alt+B already taken by another app");
        ok = false;
    }

    return ok;
}

void HotkeyManager::unregisterAll(HWND hwnd)
{
    UnregisterHotKey(hwnd, ACTION_TOGGLE_HUD);
    UnregisterHotKey(hwnd, ACTION_PRESET_MINIMAL);
    UnregisterHotKey(hwnd, ACTION_PRESET_GAMING);
    UnregisterHotKey(hwnd, ACTION_PRESET_EVERYTHING);
    UnregisterHotKey(hwnd, ACTION_CAPTURE);
}

HotkeyManager::Action HotkeyManager::identify(WPARAM wParam) const
{
    switch (wParam)
    {
    case ACTION_TOGGLE_HUD: return ACTION_TOGGLE_HUD;
    case ACTION_PRESET_MINIMAL: return ACTION_PRESET_MINIMAL;
    case ACTION_PRESET_GAMING: return ACTION_PRESET_GAMING;
    case ACTION_PRESET_EVERYTHING: return ACTION_PRESET_EVERYTHING;
    case ACTION_CAPTURE: return ACTION_CAPTURE;
    default: return ACTION_NONE;
    }
}

int HotkeyManager::presetIndex(Action a) const
{
    switch (a)
    {
    case ACTION_PRESET_MINIMAL: return 0;
    case ACTION_PRESET_GAMING: return 1;
    case ACTION_PRESET_EVERYTHING: return 2;
    default: return -1;
    }
}
