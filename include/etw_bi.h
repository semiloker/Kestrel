#ifndef ETW_BI_H
#define ETW_BI_H

#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <map>
#include <string>

#define ETW_MAX_SOURCES 12

class etw_bi
{
public:
    enum api_bi
    {
        API_NONE = 0,
        API_D3D9,
        API_D3D11,
        API_D3D12,
        API_OPENGL,
        API_VULKAN
    };

    enum provider_bi
    {
        PROV_DXGI = 0,
        PROV_D3D9,
        PROV_DXGKRNL,
        PROV_COUNT
    };

    struct sample_bi
    {
        bool valid;
        double fps;
        double frameIntervalMs;
        unsigned presents;
        DWORD pid;
        bool isForeground;
        api_bi api;

        sample_bi() : valid(false), fps(0.0), frameIntervalMs(0.0),
                      presents(0), pid(0), isForeground(false), api(API_NONE) {}
    };

    etw_bi();
    ~etw_bi();

    bool start();
    void stop();

    bool running() const { return sessionActive; }
    bool elevated() const { return isElevated; }
    const char *lastError() const { return failReason; }

    void setTarget(DWORD processId);
    DWORD target() const { return targetPid; }

    sample_bi sample();

    void setFallbackSource(provider_bi provider, unsigned eventId);
    void setDeepCensus(bool enabled) { deepCensus = enabled; }

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
    bool isElevated;
    bool deepCensus;
    const char *failReason;

    DWORD targetPid;

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
};

#endif
