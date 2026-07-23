#ifndef ETW_BI_H
#define ETW_BI_H

#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <map>
#include <string>

#include "interfaces_bi.h"

#define ETW_MAX_SOURCES 12
#define ETW_FRAME_RING 8192

class etw_bi : public IETWTrace
{
public:
    using api_bi = IETWTrace::api_bi;
    using provider_bi = IETWTrace::provider_bi;
    using sample_bi = IETWTrace::sample_bi;
    using frame_sample_bi = IETWTrace::frame_sample_bi;

    etw_bi();
    ~etw_bi();

    bool start() override;
    void stop() override;

    bool running() const override { return sessionActive; }
    bool elevated() const override { return isElevated; }
    const char *lastError() const override { return failReason; }
    bool hasFailed() const override { return failed; }

    void setTarget(DWORD processId) override;
    DWORD target() const override { return targetPid; }

    sample_bi sample() override;

    size_t drainFrames(frame_sample_bi *out, size_t max) override;
    unsigned long long framesDropped() const override { return frameLost; }

    void setFallbackSource(provider_bi provider, unsigned eventId) override;
    void setDeepCensus(bool enabled) override { deepCensus = enabled; }
    void autoConfigForApi(api_bi api) override;

    static const char *apiName(api_bi a);
    static const char *providerName(provider_bi p);
    static api_bi detectApi(DWORD processId);
    static std::string processName(DWORD processId);

private:
    struct source_def_bi
    {
        int provider;
        unsigned eventId;
        const char *name;
    };

    struct proc_source_bi
    {
        LONGLONG lastPresent100ns;
        LONGLONG intervalSum100ns;
        unsigned intervalCount;
        unsigned presentCount;
        double lastIntervalMs;
        LONGLONG lastGood100ns;
    };

    struct proc_stats_bi
    {
        proc_source_bi src[ETW_MAX_SOURCES];
        LONGLONG lastActivity100ns;

        proc_stats_bi() { memset(this, 0, sizeof(*this)); }
    };

    struct proc_ident_bi
    {
        std::string name;
        bool excluded;

        proc_ident_bi() : excluded(false) {}
    };

    static void WINAPI eventCallback(PEVENT_RECORD record);
    void onPresent(DWORD pid, int sourceIndex, LONGLONG timestamp100ns);
    void countEvent(DWORD pid, int providerIndex, unsigned eventId);

    bool evaluateProcess(proc_stats_bi &p, LONGLONG now, sample_bi *out, int *sourceOut);

    const proc_ident_bi *knownProcess(DWORD pid) const;
    const proc_ident_bi &resolveProcess(DWORD pid);

    void dumpCensus(double seconds);
    void pollHealth(LONGLONG now);

    static DWORD WINAPI traceThread(LPVOID param);

    TRACEHANDLE sessionHandle;
    TRACEHANDLE consumerHandle;
    HANDLE thread;

    volatile bool sessionActive;
    volatile bool failed;
    bool isElevated;
    bool deepCensus;
    const char *failReason;

    DWORD targetPid;
    api_bi configuredApi;

    source_def_bi sources[ETW_MAX_SOURCES];
    int sourceCount;

    DWORD reportedPid;
    int reportedSource;

    CRITICAL_SECTION lock;

    std::map<DWORD, proc_stats_bi> procs;
    std::map<DWORD, proc_ident_bi> idents;

    std::map<unsigned, unsigned> census;
    DWORD censusPid;
    LONGLONG censusStart100ns;
    LONGLONG lastCensusDump100ns;
    int censusRuns;

    LONGLONG lastHealth100ns;
    ULONG lastEventsLost;
    ULONG lastBuffersLost;

    frame_sample_bi frameRing[ETW_FRAME_RING];
    unsigned long long frameWritten;
    unsigned long long frameDrained;
    unsigned long long frameLost;
    DWORD frameOwnerPid;
};

#endif
