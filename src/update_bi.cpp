#include "update_bi.h"

#include "app_identity_bi.h"
#include "paths_bi.h"
#include "logger_bi.h"

#include <winhttp.h>
#include <winver.h>
#include <shellapi.h>
#include <cstdio>
#include <vector>

namespace
{
    const char *API_HOST = "api.github.com";
    const char *API_PATH = "/repos/semiloker/Kestrel/releases/latest";

    const size_t MIN_BINARY_BYTES = 64 * 1024;
    const size_t MAX_BINARY_BYTES = 128 * 1024 * 1024;

    std::wstring widen(const std::string &s)
    {
        if (s.empty())
            return std::wstring();

        int need = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
        if (need <= 0)
            return std::wstring();

        std::wstring out((size_t)need, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], need);
        return out;
    }

    std::string jsonValue(const std::string &json, const std::string &key, size_t from)
    {
        std::string needle = "\"" + key + "\"";
        size_t at = json.find(needle, from);
        if (at == std::string::npos)
            return std::string();

        size_t colon = json.find(':', at + needle.size());
        if (colon == std::string::npos)
            return std::string();

        size_t open = json.find('"', colon);
        if (open == std::string::npos)
            return std::string();

        std::string out;
        for (size_t i = open + 1; i < json.size(); ++i)
        {
            if (json[i] == '\\' && i + 1 < json.size())
            {
                out += json[i + 1];
                ++i;
                continue;
            }
            if (json[i] == '"')
                break;

            out += json[i];
        }
        return out;
    }

    std::string findExeAsset(const std::string &json)
    {
        size_t at = 0;
        const std::string needle = "\"browser_download_url\"";

        while ((at = json.find(needle, at)) != std::string::npos)
        {
            std::string url = jsonValue(json, "browser_download_url", at);
            at += needle.size();

            if (url.size() > 4)
            {
                std::string tail = url.substr(url.size() - 4);
                if (tail == ".exe" || tail == ".EXE")
                    return url;
            }
        }
        return std::string();
    }

    bool parseVersion(const std::string &text, int *major, int *minor, int *patch)
    {
        size_t i = 0;
        while (i < text.size() && (text[i] == 'v' || text[i] == 'V' || text[i] == ' '))
            ++i;

        int a = 0, b = 0, c = 0;
        if (sscanf(text.c_str() + i, "%d.%d.%d", &a, &b, &c) < 2)
            return false;

        *major = a;
        *minor = b;
        *patch = c;
        return true;
    }

    bool isNewer(int major, int minor, int patch)
    {
        if (major != APP_VERSION_MAJOR)
            return major > APP_VERSION_MAJOR;
        if (minor != APP_VERSION_MINOR)
            return minor > APP_VERSION_MINOR;
        return patch > APP_VERSION_PATCH;
    }

    bool httpFetch(const std::wstring &host, const std::wstring &path, bool secure,
                   std::string *body, volatile LONG *cancelled, volatile LONG *progressOut,
                   long long expectedTotal)
    {
        bool ok = false;

        HINTERNET session = WinHttpOpen(L"Kestrel-Updater/1.0",
                                        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session)
            return false;

        int timeout = 30000;
        WinHttpSetTimeouts(session, timeout, timeout, timeout, timeout);

        HINTERNET connect = WinHttpConnect(session, host.c_str(),
                                           secure ? INTERNET_DEFAULT_HTTPS_PORT
                                                  : INTERNET_DEFAULT_HTTP_PORT,
                                           0);
        if (connect)
        {
            HINTERNET request = WinHttpOpenRequest(connect, L"GET", path.c_str(), NULL,
                                                   WINHTTP_NO_REFERER,
                                                   WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                   secure ? WINHTTP_FLAG_SECURE : 0);
            if (request)
            {
                const wchar_t *headers = L"Accept: application/vnd.github+json\r\n"
                                         L"User-Agent: Kestrel-Updater\r\n";

                if (WinHttpSendRequest(request, headers, (DWORD)-1,
                                       WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                    WinHttpReceiveResponse(request, NULL))
                {
                    DWORD status = 0;
                    DWORD statusSize = sizeof(status);
                    WinHttpQueryHeaders(request,
                                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                                        WINHTTP_NO_HEADER_INDEX);

                    long long total = expectedTotal;
                    if (total <= 0)
                    {
                        DWORD len = 0;
                        DWORD lenSize = sizeof(len);
                        if (WinHttpQueryHeaders(request,
                                                WINHTTP_QUERY_CONTENT_LENGTH |
                                                    WINHTTP_QUERY_FLAG_NUMBER,
                                                WINHTTP_HEADER_NAME_BY_INDEX, &len, &lenSize,
                                                WINHTTP_NO_HEADER_INDEX))
                        {
                            total = (long long)len;
                        }
                    }

                    if (status == 200)
                    {
                        std::vector<char> chunk(16384);
                        ok = true;

                        for (;;)
                        {
                            if (cancelled && InterlockedCompareExchange(cancelled, 0, 0) != 0)
                            {
                                ok = false;
                                break;
                            }

                            DWORD read = 0;
                            if (!WinHttpReadData(request, &chunk[0], (DWORD)chunk.size(), &read))
                            {
                                ok = false;
                                break;
                            }

                            if (read == 0)
                                break;

                            body->append(&chunk[0], read);

                            if (body->size() > MAX_BINARY_BYTES)
                            {
                                ok = false;
                                break;
                            }

                            if (progressOut && total > 0)
                            {
                                LONG pct = (LONG)((body->size() * 100) / (size_t)total);
                                InterlockedExchange(progressOut, pct > 100 ? 100 : pct);
                            }
                        }
                    }
                    else
                    {
                        log_bi::write("update: HTTP status %u", (unsigned)status);
                    }
                }

                WinHttpCloseHandle(request);
            }
            WinHttpCloseHandle(connect);
        }

        WinHttpCloseHandle(session);
        return ok;
    }

    bool fetchUrl(const std::string &url, std::string *body, volatile LONG *cancelled,
                  volatile LONG *progressOut)
    {
        std::wstring wide = widen(url);

        URL_COMPONENTS parts;
        ZeroMemory(&parts, sizeof(parts));
        parts.dwStructSize = sizeof(parts);

        wchar_t hostBuf[256] = {0};
        wchar_t pathBuf[2048] = {0};
        wchar_t extraBuf[2048] = {0};

        parts.lpszHostName = hostBuf;
        parts.dwHostNameLength = 255;
        parts.lpszUrlPath = pathBuf;
        parts.dwUrlPathLength = 2047;
        parts.lpszExtraInfo = extraBuf;
        parts.dwExtraInfoLength = 2047;

        if (!WinHttpCrackUrl(wide.c_str(), (DWORD)wide.size(), 0, &parts))
            return false;

        std::wstring path = pathBuf;
        path += extraBuf;

        return httpFetch(hostBuf, path, parts.nScheme == INTERNET_SCHEME_HTTPS,
                         body, cancelled, progressOut, 0);
    }

    bool writeFile(const std::string &path, const std::string &data)
    {
        FILE *f = fopen(path.c_str(), "wb");
        if (!f)
            return false;

        size_t written = fwrite(data.data(), 1, data.size(), f);
        fclose(f);

        return written == data.size();
    }
}

update_bi::update_bi()
    : current(UPDATE_IDLE), progress(0), notifyWindow(NULL),
      worker(NULL), wantDownload(false), cancelled(0)
{
    InitializeCriticalSection(&lock);
}

update_bi::~update_bi()
{
    cancel();
    joinWorker();
    DeleteCriticalSection(&lock);
}

std::string update_bi::exeDirectory()
{
    std::string exe = paths_bi::exePath();
    size_t slash = exe.find_last_of("\\/");

    if (slash == std::string::npos)
        return std::string();

    return exe.substr(0, slash + 1);
}

std::string update_bi::stagedPath()
{
    std::string dir = exeDirectory();
    return dir.empty() ? std::string() : dir + "kestrel.new.exe";
}

std::string update_bi::backupPath()
{
    std::string dir = exeDirectory();
    return dir.empty() ? std::string() : dir + "kestrel.old.exe";
}

bool update_bi::backupExists() const
{
    std::string path = backupPath();
    if (path.empty())
        return false;

    DWORD attrs = GetFileAttributesA(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

std::string update_bi::backupVersion() const
{
    if (!backupExists())
        return std::string();

    std::string path = backupPath();
    DWORD handle = 0;
    DWORD size = GetFileVersionInfoSizeA(path.c_str(), &handle);
    if (size == 0)
        return std::string();

    std::vector<BYTE> buffer(size);
    if (!GetFileVersionInfoA(path.c_str(), 0, size, &buffer[0]))
        return std::string();

    VS_FIXEDFILEINFO *info = NULL;
    UINT infoLen = 0;
    if (!VerQueryValueA(&buffer[0], "\\", (LPVOID *)&info, &infoLen) || !info)
        return std::string();

    char text[64];
    snprintf(text, sizeof(text), "%u.%u.%u",
             (unsigned)HIWORD(info->dwFileVersionMS),
             (unsigned)LOWORD(info->dwFileVersionMS),
             (unsigned)HIWORD(info->dwFileVersionLS));

    return text;
}

update_bi::state_bi update_bi::state() const
{
    EnterCriticalSection(&lock);
    state_bi s = current;
    LeaveCriticalSection(&lock);
    return s;
}

std::string update_bi::latestVersion() const
{
    EnterCriticalSection(&lock);
    std::string v = version;
    LeaveCriticalSection(&lock);
    return v;
}

std::string update_bi::message() const
{
    EnterCriticalSection(&lock);
    std::string m = note;
    LeaveCriticalSection(&lock);
    return m;
}

int update_bi::progressPercent() const
{
    return (int)InterlockedCompareExchange(
        const_cast<volatile LONG *>(&progress), 0, 0);
}

bool update_bi::busy() const
{
    state_bi s = state();
    return s == UPDATE_CHECKING || s == UPDATE_DOWNLOADING;
}

void update_bi::publish(state_bi s, const std::string &msg)
{
    EnterCriticalSection(&lock);
    current = s;
    note = msg;
    HWND target = notifyWindow;
    LeaveCriticalSection(&lock);

    if (target && IsWindow(target))
        PostMessage(target, WM_APP_UPDATE, 0, 0);
}

void update_bi::cancel()
{
    InterlockedExchange(&cancelled, 1);
}

void update_bi::joinWorker()
{
    HANDLE handle = NULL;

    EnterCriticalSection(&lock);
    handle = worker;
    worker = NULL;
    LeaveCriticalSection(&lock);

    if (handle)
    {
        WaitForSingleObject(handle, 35000);
        CloseHandle(handle);
    }
}

void update_bi::startWorker(HWND notify, bool download)
{
    if (busy())
        return;

    joinWorker();

    InterlockedExchange(&progress, 0);

    EnterCriticalSection(&lock);
    notifyWindow = notify;
    wantDownload = download;
    current = download ? UPDATE_DOWNLOADING : UPDATE_CHECKING;
    note = download ? "Downloading" : "Checking";
    LeaveCriticalSection(&lock);

    InterlockedExchange(&cancelled, 0);

    HANDLE handle = CreateThread(NULL, 0, &update_bi::threadEntry, this, 0, NULL);

    EnterCriticalSection(&lock);
    worker = handle;
    LeaveCriticalSection(&lock);

    if (!handle)
        publish(UPDATE_FAILED, "Could not start the update thread");
}

void update_bi::checkAsync(HWND notify)
{
    startWorker(notify, false);
}

void update_bi::downloadAsync(HWND notify)
{
    startWorker(notify, true);
}

DWORD WINAPI update_bi::threadEntry(LPVOID param)
{
    update_bi *self = (update_bi *)param;

    EnterCriticalSection(&self->lock);
    bool download = self->wantDownload;
    LeaveCriticalSection(&self->lock);

    if (download)
        self->runDownload();
    else
        self->runCheck();

    return 0;
}

void update_bi::runCheck()
{
    std::string body;

    if (!httpFetch(widen(API_HOST), widen(API_PATH), true, &body, &cancelled, NULL, 0))
    {
        publish(UPDATE_FAILED, "Could not reach GitHub");
        return;
    }

    std::string tag = jsonValue(body, "tag_name", 0);
    if (tag.empty())
    {
        publish(UPDATE_FAILED, "No release information returned");
        return;
    }

    int major = 0, minor = 0, patch = 0;
    if (!parseVersion(tag, &major, &minor, &patch))
    {
        publish(UPDATE_FAILED, "Release tag is not a version number");
        return;
    }

    std::string url = findExeAsset(body);

    EnterCriticalSection(&lock);
    version = tag;
    assetUrl = url;
    LeaveCriticalSection(&lock);

    if (!isNewer(major, minor, patch))
    {
        publish(UPDATE_CURRENT, "You have the newest release");
        return;
    }

    if (url.empty())
    {
        publish(UPDATE_FAILED, "Release has no .exe attached");
        return;
    }

    log_bi::write("update: %s available (current %s)", tag.c_str(), APP_VERSION_STRING);
    publish(UPDATE_AVAILABLE, "Update available");
}

void update_bi::runDownload()
{
    EnterCriticalSection(&lock);
    std::string url = assetUrl;
    LeaveCriticalSection(&lock);

    if (url.empty())
    {
        publish(UPDATE_FAILED, "Nothing to download");
        return;
    }

    std::string staged = stagedPath();
    if (staged.empty())
    {
        publish(UPDATE_FAILED, "Cannot locate the program folder");
        return;
    }

    std::string payload;

    if (!fetchUrl(url, &payload, &cancelled, &progress))
    {
        publish(UPDATE_FAILED, "Download failed or was cancelled");
        return;
    }

    InterlockedExchange(&progress, 100);

    if (payload.size() < MIN_BINARY_BYTES || payload.compare(0, 2, "MZ") != 0)
    {
        publish(UPDATE_FAILED, "Downloaded file is not a Windows program");
        return;
    }

    if (!writeFile(staged, payload))
    {
        publish(UPDATE_FAILED, "Cannot write to the program folder");
        return;
    }

    log_bi::write("update: staged %u bytes at %s",
                  (unsigned)payload.size(), staged.c_str());

    publish(UPDATE_READY, "Ready to install");
}

bool update_bi::applyAndRestart()
{
    std::string exe = paths_bi::exePath();
    std::string staged = stagedPath();
    std::string backup = backupPath();

    if (exe.empty() || staged.empty() || backup.empty())
        return false;

    if (GetFileAttributesA(staged.c_str()) == INVALID_FILE_ATTRIBUTES)
        return false;

    DeleteFileA(backup.c_str());

    if (!MoveFileExA(exe.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        log_bi::writeErr(GetLastError(), "update: cannot move the running program aside");
        return false;
    }

    if (!MoveFileExA(staged.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        log_bi::writeErr(GetLastError(), "update: cannot put the new program in place");
        MoveFileExA(backup.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING);
        return false;
    }

    log_bi::write("update: installed, restarting");

    HINSTANCE result = ShellExecuteA(NULL, "open", exe.c_str(), NULL, NULL, SW_SHOWNORMAL);
    return (INT_PTR)result > 32;
}

bool update_bi::rollback()
{
    std::string exe = paths_bi::exePath();
    std::string backup = backupPath();
    std::string staged = stagedPath();

    if (exe.empty() || backup.empty() || !backupExists())
        return false;

    DeleteFileA(staged.c_str());

    if (!MoveFileExA(exe.c_str(), staged.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        log_bi::writeErr(GetLastError(), "rollback: cannot move the current program aside");
        return false;
    }

    if (!MoveFileExA(backup.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        log_bi::writeErr(GetLastError(), "rollback: cannot restore the previous program");
        MoveFileExA(staged.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING);
        return false;
    }

    log_bi::write("rollback: previous version restored, restarting");

    HINSTANCE result = ShellExecuteA(NULL, "open", exe.c_str(), NULL, NULL, SW_SHOWNORMAL);
    return (INT_PTR)result > 32;
}
