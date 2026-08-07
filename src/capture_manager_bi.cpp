#include "capture_manager_bi.h"
#include "logger_bi.h"

#include <utility>

capture_manager_bi::capture_manager_bi()
{
    InitializeCriticalSection(&finalizeLock);
}

capture_manager_bi::~capture_manager_bi()
{
    stopActive();
    waitForFinalize();
    DeleteCriticalSection(&finalizeLock);
}

DWORD WINAPI capture_manager_bi::finalizeEntry(LPVOID param)
{
    capture_manager_bi *manager = static_cast<capture_manager_bi *>(param);

    capture_bi::summary_bi summary;
    bool succeeded = false;
    try
    {
        succeeded = manager->finalizeCapture &&
                    manager->finalizeCapture->stop(&summary);
    }
    catch (...)
    {
        // stop() writes the frame CSV and appends to index.csv; a throw here
        // used to leave no trace at all.
        log_bi::write("capture: finalize threw, the run was not saved");
        succeeded = false;
    }

    EnterCriticalSection(&manager->finalizeLock);
    manager->pendingSummary = summary;
    manager->pendingSucceeded = succeeded;
    manager->pendingReady = true;
    LeaveCriticalSection(&manager->finalizeLock);
    return 0;
}

void capture_manager_bi::beginFinalize()
{
    if (!cap_.active() || finalizeThread)
        return;

    finalizeCapture.reset(new capture_bi(std::move(cap_)));
    cap_ = capture_bi();

    EnterCriticalSection(&finalizeLock);
    pendingReady = false;
    pendingSucceeded = false;
    LeaveCriticalSection(&finalizeLock);
    lastFinalizeFailed = false;

    finalizeThread = CreateThread(
        NULL, 0, &capture_manager_bi::finalizeEntry, this, 0, NULL);
    if (!finalizeThread)
    {
        finalizeEntry(this);

        capture_bi::summary_bi summary;
        bool succeeded = false;
        EnterCriticalSection(&finalizeLock);
        summary = pendingSummary;
        succeeded = pendingSucceeded;
        pendingReady = false;
        LeaveCriticalSection(&finalizeLock);

        finalizeCapture.reset();
        lastFinalizeFailed = !succeeded;
        if (succeeded)
        {
            lastCapture = summary;
            haveLastCapture = true;
            captureHistoryLoaded = false;
        }
        else
        {
            log_bi::write("capture: finalize failed inline, the run was not saved");
        }
    }
}

void capture_manager_bi::pollFinalize()
{
    if (!finalizeThread ||
        WaitForSingleObject(finalizeThread, 0) != WAIT_OBJECT_0)
    {
        return;
    }

    CloseHandle(finalizeThread);
    finalizeThread = NULL;

    capture_bi::summary_bi summary;
    bool ready = false;
    bool succeeded = false;
    EnterCriticalSection(&finalizeLock);
    ready = pendingReady;
    succeeded = pendingSucceeded;
    summary = pendingSummary;
    pendingReady = false;
    LeaveCriticalSection(&finalizeLock);

    finalizeCapture.reset();
    lastFinalizeFailed = !(ready && succeeded);
    if (ready && succeeded)
    {
        lastCapture = summary;
        haveLastCapture = true;
        captureHistoryLoaded = false;
    }
    else if (ready)
    {
        log_bi::write("capture: finalize failed, the run was not saved");
    }
    else
    {
        log_bi::write("capture: finalize produced no result, the run was not saved");
    }
}

void capture_manager_bi::waitForFinalize()
{
    if (finalizeThread)
        WaitForSingleObject(finalizeThread, INFINITE);
    pollFinalize();
}

bool capture_manager_bi::finalizing()
{
    pollFinalize();
    return finalizeThread != NULL;
}

void capture_manager_bi::toggle(const std::string &processName, DWORD processId)
{
    pollFinalize();

    if (cap_.active())
    {
        beginFinalize();
    }
    else if (!finalizeThread)
    {
        lastFinalizeFailed = false;
        cap_.start(processName, processId);
    }
}

void capture_manager_bi::stopActive()
{
    pollFinalize();
    if (cap_.active())
        beginFinalize();
}

void capture_manager_bi::loadHistory()
{
    pollFinalize();
    if (!captureHistoryLoaded)
    {
        capture_bi::loadIndex(&captureHistory);
        captureHistoryLoaded = true;
    }
}
