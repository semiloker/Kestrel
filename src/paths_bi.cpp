#include "paths_bi.h"
#include "app_identity_bi.h"

#include <windows.h>
#include <shlobj.h>
#include <vector>

namespace
{
    std::wstring g_dataDirWide;
    std::string g_dataDir;
    std::wstring g_exePathWide;
    std::string g_exePath;
    bool g_dataDirReady = false;
    bool g_exePathReady = false;

    std::string toUtf8(const std::wstring &text)
    {
        if (text.empty())
            return std::string();

        int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                        text.c_str(), (int)text.size(),
                                        NULL, 0, NULL, NULL);
        if (bytes <= 0)
            return std::string();

        std::string result((size_t)bytes, '\0');
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                            text.c_str(), (int)text.size(),
                            &result[0], bytes, NULL, NULL);
        return result;
    }

    std::wstring fromUtf8(const char *text)
    {
        if (!text || !*text)
            return std::wstring();

        int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                        text, -1, NULL, 0);
        if (chars <= 1)
            return std::wstring();

        std::wstring result((size_t)chars, L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                            text, -1, &result[0], chars);
        result.resize((size_t)chars - 1);
        return result;
    }
}

std::string paths_bi::wideToUtf8(const std::wstring &text)
{
    return toUtf8(text);
}

std::wstring paths_bi::utf8ToWide(const std::string &text)
{
    return fromUtf8(text.c_str());
}

const std::wstring &paths_bi::dataDirWide()
{
    if (g_dataDirReady)
        return g_dataDirWide;

    g_dataDirReady = true;

    PWSTR roaming = NULL;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT,
                                       NULL, &roaming)) &&
        roaming)
    {
        std::wstring dir = std::wstring(roaming) + L"\\" APP_DATA_DIR_WIDE;
        CoTaskMemFree(roaming);

        if (CreateDirectoryW(dir.c_str(), NULL))
        {
            g_dataDirWide = dir;
        }
        else if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            DWORD attributes = GetFileAttributesW(dir.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES &&
                (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
                (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
            {
                g_dataDirWide = dir;
            }
        }
    }

    g_dataDir = toUtf8(g_dataDirWide);
    return g_dataDirWide;
}

const std::string &paths_bi::dataDir()
{
    dataDirWide();
    return g_dataDir;
}

std::wstring paths_bi::inDataDirWide(const wchar_t *fileName)
{
    const std::wstring &dir = dataDirWide();
    if (dir.empty())
        return std::wstring();

    return dir + L"\\" + (fileName ? fileName : L"");
}

std::string paths_bi::inDataDir(const char *fileName)
{
    std::wstring wideName = fromUtf8(fileName);
    if (wideName.empty() && fileName && *fileName)
        return std::string();
    return toUtf8(inDataDirWide(wideName.c_str()));
}

const std::wstring &paths_bi::exePathWide()
{
    if (g_exePathReady)
        return g_exePathWide;

    g_exePathReady = true;

    std::vector<wchar_t> buffer(512);
    while (buffer.size() <= 32768)
    {
        SetLastError(ERROR_SUCCESS);
        DWORD chars = GetModuleFileNameW(NULL, &buffer[0], (DWORD)buffer.size());
        if (chars == 0)
            break;
        if (chars < buffer.size())
        {
            g_exePathWide.assign(&buffer[0], chars);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }

    g_exePath = toUtf8(g_exePathWide);
    return g_exePathWide;
}

const std::string &paths_bi::exePath()
{
    exePathWide();

    return g_exePath;
}
