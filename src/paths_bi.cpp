#include "paths_bi.h"
#include "app_identity_bi.h"

#include <windows.h>
#include <shlobj.h>

namespace
{
    std::string g_dataDir;
    std::string g_exePath;
    bool g_dataDirReady = false;
    bool g_exePathReady = false;
}

const std::string &paths_bi::dataDir()
{
    if (g_dataDirReady)
        return g_dataDir;

    g_dataDirReady = true;

    char roaming[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, roaming)))
    {
        std::string dir = std::string(roaming) + "\\" APP_DATA_DIR;

        if (CreateDirectoryA(dir.c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS)
            g_dataDir = dir;
    }

    return g_dataDir;
}

std::string paths_bi::inDataDir(const char *fileName)
{
    const std::string &dir = dataDir();
    if (dir.empty())
        return std::string();

    return dir + "\\" + fileName;
}

const std::string &paths_bi::exePath()
{
    if (g_exePathReady)
        return g_exePath;

    g_exePathReady = true;

    char buf[MAX_PATH] = {0};
    DWORD n = GetModuleFileNameA(NULL, buf, MAX_PATH);

    if (n > 0 && n < MAX_PATH)
        g_exePath.assign(buf, n);

    return g_exePath;
}
