#ifndef CAPTURE_MANAGER_BI_H
#define CAPTURE_MANAGER_BI_H

#include <memory>
#include <string>
#include <vector>

#include "capture_bi.h"

class capture_manager_bi
{
public:
    capture_manager_bi();
    ~capture_manager_bi();
    capture_manager_bi(const capture_manager_bi &) = delete;
    capture_manager_bi &operator=(const capture_manager_bi &) = delete;

    void toggle(const std::string &processName, DWORD processId);
    bool active() const { return cap_.active(); }
    bool finalizing();

    void stopActive();
    void pollFinalize();
    void waitForFinalize();
    void loadHistory();

    capture_bi &getCapture() { return cap_; }
    const capture_bi &getCapture() const { return cap_; }

    const capture_bi::summary_bi &lastSummary() const { return lastCapture; }
    bool hasLastSummary() const { return haveLastCapture; }

    // True when the most recent finalize could not write the run out. Without
    // this the run just vanishes from history with nothing in the log, which
    // is the same symptom 1.4.3 fixed on the recording side.
    bool finalizeFailed() const { return lastFinalizeFailed; }

    const std::vector<capture_bi::summary_bi> &history() const { return captureHistory; }
    std::vector<capture_bi::summary_bi> &history() { return captureHistory; }

    bool historyLoaded() const { return captureHistoryLoaded; }

private:
    capture_bi cap_;
    capture_bi::summary_bi lastCapture;
    bool haveLastCapture = false;
    bool lastFinalizeFailed = false;
    std::vector<capture_bi::summary_bi> captureHistory;
    bool captureHistoryLoaded = false;

    void beginFinalize();
    static DWORD WINAPI finalizeEntry(LPVOID param);

    CRITICAL_SECTION finalizeLock;
    HANDLE finalizeThread = NULL;
    std::unique_ptr<capture_bi> finalizeCapture;
    capture_bi::summary_bi pendingSummary;
    bool pendingReady = false;
    bool pendingSucceeded = false;
};

#endif
