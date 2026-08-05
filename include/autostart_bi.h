#ifndef AUTOSTART_BI_H
#define AUTOSTART_BI_H

#include <windows.h>

namespace autostart_bi
{
    enum mode_bi
    {
        AUTOSTART_OFF = 0,
        AUTOSTART_NORMAL,
        AUTOSTART_ADMIN
    };

    bool isElevated();
    bool executablePathProtected();

    mode_bi current();

    bool taskExists();

    bool taskPointsToThisExe();

    bool setMode(mode_bi mode);

    bool runTask();

    // Elevation fallback for an executable that does not live in an
    // administrator-protected directory, where a highest-privilege scheduled
    // task would be a privilege-escalation hole. Costs one UAC prompt.
    bool elevationRequested();
    bool elevateSelf();

    bool handleCommandLine(const char *cmdLine, int *exitCode);

    extern const char *ARG_INSTALL_TASK;
    extern const char *ARG_REMOVE_TASK;

    extern const char *ARG_FROM_TASK;
    extern const char *ARG_AUTOSTART;
}

#endif
