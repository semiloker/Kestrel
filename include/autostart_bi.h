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

    mode_bi current();

    bool taskExists();

    bool setMode(mode_bi mode);

    bool runTask();

    bool handleCommandLine(const char *cmdLine, int *exitCode);

    extern const char *ARG_INSTALL_TASK;
    extern const char *ARG_REMOVE_TASK;

    extern const char *ARG_FROM_TASK;
}

#endif
