#include "hotkey_manager_bi.h"
#include "logger_bi.h"

bool HotkeyManager::registerAll(HWND hwnd)
{
    struct binding_bi
    {
        Action action;
        UINT key;
        const char *label;
    };

    const binding_bi bindings[] = {
        {ACTION_TOGGLE_HUD, 'H', "Ctrl+Alt+H"},
        {ACTION_PRESET_MINIMAL, '1', "Ctrl+Alt+1"},
        {ACTION_PRESET_GAMING, '2', "Ctrl+Alt+2"},
        {ACTION_PRESET_EVERYTHING, '3', "Ctrl+Alt+3"},
        {ACTION_CAPTURE, 'B', "Ctrl+Alt+B"}};

    registeredMask = 0;
    failedMask = 0;
    for (size_t i = 0; i < sizeof(bindings) / sizeof(bindings[0]); ++i)
    {
        unsigned bit = 1u << (unsigned)bindings[i].action;
        if (RegisterHotKey(hwnd, bindings[i].action,
                           MOD_CONTROL | MOD_ALT | MOD_NOREPEAT,
                           bindings[i].key))
        {
            registeredMask |= bit;
        }
        else
        {
            failedMask |= bit;
            log_bi::writeErr(GetLastError(), "hotkey: %s is unavailable",
                             bindings[i].label);
        }
    }

    return failedMask == 0;
}

void HotkeyManager::unregisterAll(HWND hwnd)
{
    for (unsigned action = ACTION_TOGGLE_HUD; action <= ACTION_CAPTURE; ++action)
    {
        unsigned bit = 1u << action;
        if ((registeredMask & bit) != 0)
            UnregisterHotKey(hwnd, (int)action);
    }
    registeredMask = 0;
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

std::string HotkeyManager::conflictSummary() const
{
    if (failedMask == 0)
        return std::string();

    const char *labels[] = {
        "", "Ctrl+Alt+H", "Ctrl+Alt+1", "Ctrl+Alt+2", "Ctrl+Alt+3", "Ctrl+Alt+B"};

    std::string result;
    for (unsigned action = ACTION_TOGGLE_HUD; action <= ACTION_CAPTURE; ++action)
    {
        if ((failedMask & (1u << action)) == 0)
            continue;
        if (!result.empty())
            result += ", ";
        result += labels[action];
    }
    return result;
}
