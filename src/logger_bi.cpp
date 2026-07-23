#include "logger_bi.h"
#include "paths_bi.h"
#include "app_identity_bi.h"

#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <format>

namespace
{
    FILE *g_file = nullptr;
    CRITICAL_SECTION g_lock;
    bool g_lockReady = false;

    std::string stamp()
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        return std::format("{:02}:{:02}:{:02}.{:03}",
                           (unsigned)st.wHour, (unsigned)st.wMinute,
                           (unsigned)st.wSecond, (unsigned)st.wMilliseconds);
    }

    void emit(const char *line)
    {
        if (!g_file)
            return;

        std::string ts = stamp();

        EnterCriticalSection(&g_lock);
        fprintf(g_file, "[%s] %s\n", ts.c_str(), line);
        fflush(g_file);
        LeaveCriticalSection(&g_lock);
    }
}

void log_bi::init()
{
    if (g_file)
        return;

    if (!g_lockReady)
    {
        InitializeCriticalSection(&g_lock);
        g_lockReady = true;
    }

    std::string path = paths_bi::inDataDir(APP_LOG_FILE);
    if (path.empty())
        return;

    g_file = fopen(path.c_str(), "w");
    if (!g_file)
        return;

    bool elevated = false;
    HANDLE token = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        TOKEN_ELEVATION el;
        DWORD size = sizeof(el);
        if (GetTokenInformation(token, TokenElevation, &el, sizeof(el), &size))
            elevated = (el.TokenIsElevated != 0);
        CloseHandle(token);
    }

    write("%s %s starting", APP_NAME, APP_VERSION_STRING);
    write("exe: %s", paths_bi::exePath().c_str());
    write("elevated: %s", elevated ? "yes" : "NO (frame metrics need admin)");
}

void log_bi::write(const char *fmt, ...)
{
    if (!g_file)
        return;

    char line[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    emit(line);
}

void log_bi::writeErr(unsigned long code, const char *fmt, ...)
{
    if (!g_file)
        return;

    char line[1024];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    if (n < 0)
        return;
    if ((size_t)n >= sizeof(line))
        n = (int)sizeof(line) - 1;

    char *text = nullptr;
    DWORD chars = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&text, 0, NULL);

    std::string detail;
    if (chars && text)
    {
        detail = text;
        while (!detail.empty() && (detail.back() == '\r' || detail.back() == '\n'))
            detail.pop_back();
    }
    if (text)
        LocalFree(text);

    std::string suffix = std::format(" [0x{:08X}: {}]",
                                     code, detail.empty() ? "no description" : detail);
    size_t remain = sizeof(line) - (size_t)n - 1;
    strncpy(line + n, suffix.c_str(), remain);
    line[sizeof(line) - 1] = '\0';

    emit(line);
}

void log_bi::shutdown()
{
    if (!g_file)
        return;

    write("shutting down");

    EnterCriticalSection(&g_lock);
    fclose(g_file);
    g_file = nullptr;
    LeaveCriticalSection(&g_lock);
}
