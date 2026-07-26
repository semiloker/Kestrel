#ifndef UPDATE_BI_H
#define UPDATE_BI_H

#include <windows.h>
#include <string>

#define WM_APP_UPDATE (WM_APP + 17)

class update_bi
{
public:
    enum state_bi
    {
        UPDATE_IDLE = 0,
        UPDATE_CHECKING,
        UPDATE_CURRENT,
        UPDATE_AVAILABLE,
        UPDATE_DOWNLOADING,
        UPDATE_READY,
        UPDATE_FAILED
    };

    update_bi();
    ~update_bi();

    update_bi(const update_bi &) = delete;
    update_bi &operator=(const update_bi &) = delete;

    void checkAsync(HWND notify);
    void downloadAsync(HWND notify);
    void cancel();

    // On success the caller owns a deny-write/delete verification handle and
    // must keep it open through creation of the replacement process.
    bool apply(HANDLE *verifiedGuardOut);
    bool rollback(HANDLE *verifiedGuardOut);

    bool backupExists() const;
    std::string backupVersion() const;

    state_bi state() const;
    std::string latestVersion() const;
    std::string message() const;
    int progressPercent() const;
    bool busy() const;

    static std::wstring exeDirectory();
    static std::wstring stagedPath();
    static std::wstring backupPath();

private:
    static DWORD WINAPI threadEntry(LPVOID param);

    void runCheck();
    void runDownload();
    void publish(state_bi s, const std::string &msg);
    void startWorker(HWND notify, bool download);
    void joinWorker();

    mutable CRITICAL_SECTION lock;

    state_bi current;
    std::string version;
    std::string assetUrl;
    std::string checksumUrl;
    std::string expectedSha256;
    bool stagedSigned;
    std::string note;
    volatile LONG progress;

    HWND notifyWindow;
    HANDLE worker;
    bool wantDownload;
    volatile LONG cancelled;
};

#endif
