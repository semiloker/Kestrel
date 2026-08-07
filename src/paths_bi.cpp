#include "paths_bi.h"
#include "app_identity_bi.h"

#include <windows.h>
#include <shlobj.h>
#include <appmodel.h>
#include <cstdint>
#include <fcntl.h>
#include <io.h>
#include <vector>

namespace
{
    // A full-trust app launched from an installed MSIX package has its
    // %APPDATA% (Roaming) file I/O silently redirected by the OS to a
    // per-package folder under LOCALAPPDATA\Packages\<FamilyName>\LocalCache.
    // That's transparent to this process's own reads/writes, but Explorer -
    // opened unpackaged by the "Open folder" button - resolves the logical
    // %APPDATA%\Kestrel path and finds it empty. Resolve the real physical
    // folder directly so both this process and Explorer agree on it.
    std::wstring packagedRoamingBase()
    {
        UINT32 length = 0;
        if (GetCurrentPackageFamilyName(&length, NULL) != ERROR_INSUFFICIENT_BUFFER ||
            length == 0)
        {
            return std::wstring();
        }

        std::vector<wchar_t> familyName(length);
        if (GetCurrentPackageFamilyName(&length, &familyName[0]) != ERROR_SUCCESS)
            return std::wstring();

        PWSTR local = NULL;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT,
                                         NULL, &local)) ||
            !local)
        {
            if (local)
                CoTaskMemFree(local);
            return std::wstring();
        }

        std::wstring base = std::wstring(local) + L"\\Packages\\" +
                             std::wstring(&familyName[0]) + L"\\LocalCache\\Roaming";
        CoTaskMemFree(local);
        return base;
    }

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

    std::wstring roamingBase = packagedRoamingBase();
    bool havePackagedBase = !roamingBase.empty();

    PWSTR roaming = NULL;
    if (havePackagedBase ||
        (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT,
                                        NULL, &roaming)) &&
         roaming))
    {
        std::wstring dir = (havePackagedBase ? roamingBase : std::wstring(roaming)) +
                           L"\\" APP_DATA_DIR_WIDE;
        if (roaming)
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

FILE *paths_bi::openRegularAppend(const std::wstring &path, bool *emptyOut)
{
    if (emptyOut)
        *emptyOut = false;

    HANDLE handle = CreateFileW(
        path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (handle == INVALID_HANDLE_VALUE)
        return NULL;

    FILE_ATTRIBUTE_TAG_INFO attributes = {};
    BY_HANDLE_FILE_INFORMATION information = {};
    if (GetFileType(handle) != FILE_TYPE_DISK ||
        !GetFileInformationByHandleEx(
            handle, FileAttributeTagInfo, &attributes, sizeof(attributes)) ||
        (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
        !GetFileInformationByHandle(handle, &information) ||
        information.nNumberOfLinks != 1)
    {
        CloseHandle(handle);
        return NULL;
    }

    if (emptyOut)
        *emptyOut = information.nFileSizeHigh == 0 &&
                    information.nFileSizeLow == 0;

    LARGE_INTEGER offset = {};
    if (!SetFilePointerEx(handle, offset, NULL, FILE_END))
    {
        CloseHandle(handle);
        return NULL;
    }

    int fd = _open_osfhandle(
        (intptr_t)handle, _O_WRONLY | _O_BINARY | _O_APPEND);
    if (fd < 0)
    {
        CloseHandle(handle);
        return NULL;
    }

    FILE *file = _fdopen(fd, "ab");
    if (!file)
        _close(fd);
    return file;
}
