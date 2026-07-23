#include "etw_bi.h"
#include "logger_bi.h"
#include "app_identity_bi.h"

#include <evntrace.h>
#include <evntcons.h>
#include <psapi.h>
#include <vector>
#include <algorithm>

#ifndef PROCESS_TRACE_MODE_EVENT_RECORD
#define PROCESS_TRACE_MODE_EVENT_RECORD 0x10000000
#endif
#ifndef PROCESS_TRACE_MODE_REAL_TIME
#define PROCESS_TRACE_MODE_REAL_TIME 0x00000100
#endif
#ifndef PROCESS_QUERY_LIMITED_INFORMATION
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#endif

static const wchar_t *ETW_SESSION_NAME = APP_ETW_SESSION;

static const GUID PROVIDER_GUIDS[etw_bi::PROV_COUNT] = {
    {0xCA11C036, 0x0102, 0x4A2D, {0xA6, 0xAD, 0xF0, 0x3C, 0xFE, 0xD5, 0xD3, 0xC9}},
    {0x783ACA0A, 0x790E, 0x4D7F, {0x84, 0x51, 0xAA, 0x85, 0x05, 0x11, 0xC6, 0xB9}},
    {0x802EC45A, 0x1E99, 0x4B83, {0x99, 0x20, 0x87, 0xC9, 0x82, 0x77, 0xBA, 0x9D}}};

#define DXGKRNL_KEYWORD_PRESENT 0x0000000008000000ULL
#define DXGI_KEYWORD_PRESENT 0x4000000000000002ULL
#define D3D9_KEYWORD_PRESENT 0x8000000000000002ULL

#define ETW_STALE_100NS 20000000LL
#define ETW_PRUNE_100NS 300000000LL

#define ETW_BACKGROUND_MIN_FPS 20.0
#define ETW_SWITCH_MARGIN 1.5

#define ETW_CENSUS_WINDOW_100NS 80000000LL
#define ETW_CENSUS_COOLDOWN_100NS 300000000LL
#define ETW_CENSUS_MAX_RUNS 3

#define ETW_HEALTH_INTERVAL_100NS 100000000LL

static const char *EXCLUDED_PROCESSES[] = {
    "dwm.exe",
    "csrss.exe"};

static size_t sessionPropsSize()
{
    return sizeof(EVENT_TRACE_PROPERTIES) +
           (wcslen(ETW_SESSION_NAME) + 1) * sizeof(wchar_t) + sizeof(wchar_t) * 2;
}

static void initSessionProps(EVENT_TRACE_PROPERTIES *props, size_t size)
{
    memset(props, 0, size);
    props->Wnode.BufferSize = (ULONG)size;
    props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    props->Wnode.ClientContext = 1;
    props->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
}

etw_bi::etw_bi()
    : sessionHandle(0),
      consumerHandle(0),
      thread(NULL),
      sessionActive(false),
      failed(false),
      isElevated(false),
      deepCensus(false),
      failReason("not started"),
      targetPid(0),
      configuredApi(API_NONE),
      sourceCount(0),
      reportedPid(0),
      reportedSource(-1),
      censusPid(0),
      censusStart100ns(0),
      lastCensusDump100ns(0),
      censusRuns(0),
      lastHealth100ns(0),
      lastEventsLost(0),
      lastBuffersLost(0),
      frameWritten(0),
      frameDrained(0),
      frameLost(0),
      frameOwnerPid(0)
{
    InitializeCriticalSection(&lock);

    memset(sources, 0, sizeof(sources));
    memset(frameRing, 0, sizeof(frameRing));

    int n = 0;

    sources[n].provider = PROV_D3D9;
    sources[n].eventId = 1;
    sources[n].name = "D3D9 Present";
    ++n;

    sources[n].provider = PROV_DXGI;
    sources[n].eventId = 42;
    sources[n].name = "DXGI Present";
    ++n;

    sources[n].provider = PROV_DXGI;
    sources[n].eventId = 178;
    sources[n].name = "DXGI id 178";
    ++n;

    sources[n].provider = PROV_DXGI;
    sources[n].eventId = 74;
    sources[n].name = "DXGI id 74";
    ++n;

    sources[n].provider = PROV_DXGI;
    sources[n].eventId = 144;
    sources[n].name = "DXGI Present1";
    ++n;

    sources[n].provider = PROV_DXGKRNL;
    sources[n].eventId = 171;
    sources[n].name = "DxgKrnl PresentHistory";
    ++n;

    sources[n].provider = PROV_DXGKRNL;
    sources[n].eventId = 184;
    sources[n].name = "DxgKrnl Present";
    ++n;

    sources[n].provider = PROV_DXGKRNL;
    sources[n].eventId = 166;
    sources[n].name = "DxgKrnl Blit";
    ++n;

    sources[n].provider = PROV_DXGKRNL;
    sources[n].eventId = 168;
    sources[n].name = "DxgKrnl Flip";
    ++n;

    sourceCount = n;

    HANDLE token = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        TOKEN_ELEVATION elevation;
        DWORD size = sizeof(elevation);

        if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size))
            isElevated = (elevation.TokenIsElevated != 0);

        CloseHandle(token);
    }
}

etw_bi::~etw_bi()
{
    stop();
    DeleteCriticalSection(&lock);
}

const char *etw_bi::apiName(api_bi a)
{
    switch (a)
    {
    case API_D3D9:
        return "D3D9";
    case API_D3D11:
        return "D3D11";
    case API_D3D12:
        return "D3D12";
    case API_OPENGL:
        return "OpenGL";
    case API_VULKAN:
        return "Vulkan";
    default:
        return "-";
    }
}

const char *etw_bi::providerName(provider_bi p)
{
    switch (p)
    {
    case PROV_DXGI:
        return "DXGI";
    case PROV_D3D9:
        return "D3D9";
    case PROV_DXGKRNL:
        return "DxgKrnl";
    default:
        return "?";
    }
}

std::string etw_bi::processName(DWORD processId)
{
    if (processId == 0)
        return std::string("?");

    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!proc)
        return std::string("<access denied>");

    char path[MAX_PATH] = {0};
    DWORD size = MAX_PATH;
    std::string result;

    if (QueryFullProcessImageNameA(proc, 0, path, &size) && size > 0)
    {
        std::string full(path, size);
        size_t slash = full.find_last_of('\\');
        result = (slash == std::string::npos) ? full : full.substr(slash + 1);
    }
    else
    {
        result = "<unknown>";
    }

    CloseHandle(proc);
    return result;
}

etw_bi::api_bi etw_bi::detectApi(DWORD processId)
{
    if (processId == 0)
        return API_NONE;

    HANDLE proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
    if (!proc)
        return API_NONE;

    HMODULE modules[512];
    DWORD needed = 0;
    api_bi result = API_NONE;

    if (EnumProcessModules(proc, modules, sizeof(modules), &needed))
    {
        DWORD count = needed / sizeof(HMODULE);
        if (count > 512)
            count = 512;

        bool hasVulkan = false, hasD3D12 = false, hasD3D11 = false;
        bool hasOpenGL = false, hasD3D9 = false;

        for (DWORD i = 0; i < count; ++i)
        {
            char name[MAX_PATH];
            if (!GetModuleBaseNameA(proc, modules[i], name, sizeof(name)))
                continue;

            if (_stricmp(name, "vulkan-1.dll") == 0)
                hasVulkan = true;
            else if (_stricmp(name, "d3d12.dll") == 0 || _stricmp(name, "d3d12core.dll") == 0)
                hasD3D12 = true;
            else if (_stricmp(name, "d3d11.dll") == 0)
                hasD3D11 = true;
            else if (_stricmp(name, "opengl32.dll") == 0)
                hasOpenGL = true;
            else if (_stricmp(name, "d3d9.dll") == 0)
                hasD3D9 = true;
        }

        if (hasVulkan)
            result = API_VULKAN;
        else if (hasD3D12)
            result = API_D3D12;
        else if (hasD3D11)
            result = API_D3D11;
        else if (hasOpenGL)
            result = API_OPENGL;
        else if (hasD3D9)
            result = API_D3D9;
    }

    CloseHandle(proc);
    return result;
}

const etw_bi::proc_ident_bi *etw_bi::knownProcess(DWORD pid) const
{
    std::map<DWORD, proc_ident_bi>::const_iterator it = idents.find(pid);
    return (it == idents.end()) ? NULL : &it->second;
}

const etw_bi::proc_ident_bi &etw_bi::resolveProcess(DWORD pid)
{
    std::map<DWORD, proc_ident_bi>::iterator it = idents.find(pid);
    if (it != idents.end())
        return it->second;

    proc_ident_bi ident;
    ident.name = processName(pid);

    for (size_t i = 0; i < sizeof(EXCLUDED_PROCESSES) / sizeof(EXCLUDED_PROCESSES[0]); ++i)
    {
        if (_stricmp(ident.name.c_str(), EXCLUDED_PROCESSES[i]) == 0)
        {
            ident.excluded = true;
            break;
        }
    }

    return idents.insert(std::make_pair(pid, ident)).first->second;
}

void etw_bi::setFallbackSource(provider_bi provider, unsigned eventId)
{
    if (eventId == 0)
        return;

    EnterCriticalSection(&lock);

    if (sourceCount < ETW_MAX_SOURCES)
    {
        sources[sourceCount].provider = provider;
        sources[sourceCount].eventId = eventId;
        sources[sourceCount].name = "configured fallback";
        ++sourceCount;
    }

    LeaveCriticalSection(&lock);

    log_bi::write("etw: added fallback present source %s event %u",
                  providerName(provider), eventId);
}

void etw_bi::autoConfigForApi(api_bi api)
{
    if (api == configuredApi || api == API_NONE)
        return;

    configuredApi = api;

    EnterCriticalSection(&lock);

    switch (api)
    {
    case API_D3D9:
        if (sourceCount < ETW_MAX_SOURCES)
        {
            sources[sourceCount].provider = PROV_D3D9;
            sources[sourceCount].eventId = 1;
            sources[sourceCount].name = "D3D9 Present";
            ++sourceCount;
        }
        break;

    case API_D3D11:
    case API_D3D12:
        if (sourceCount < ETW_MAX_SOURCES)
        {
            sources[sourceCount].provider = PROV_DXGI;
            sources[sourceCount].eventId = 42;
            sources[sourceCount].name = "DXGI Present";
            ++sourceCount;
        }
        if (sourceCount < ETW_MAX_SOURCES)
        {
            sources[sourceCount].provider = PROV_DXGI;
            sources[sourceCount].eventId = 144;
            sources[sourceCount].name = "DXGI MPO Present";
            ++sourceCount;
        }
        break;

    case API_OPENGL:
        if (sourceCount < ETW_MAX_SOURCES)
        {
            sources[sourceCount].provider = PROV_DXGI;
            sources[sourceCount].eventId = 42;
            sources[sourceCount].name = "OpenGL DXGI Present";
            ++sourceCount;
        }
        if (sourceCount < ETW_MAX_SOURCES)
        {
            sources[sourceCount].provider = PROV_DXGI;
            sources[sourceCount].eventId = 144;
            sources[sourceCount].name = "OpenGL DXGI MPO";
            ++sourceCount;
        }
        if (sourceCount < ETW_MAX_SOURCES)
        {
            sources[sourceCount].provider = PROV_DXGKRNL;
            sources[sourceCount].eventId = 166;
            sources[sourceCount].name = "OpenGL Blit";
            ++sourceCount;
        }
        break;

    case API_VULKAN:
        if (sourceCount < ETW_MAX_SOURCES)
        {
            sources[sourceCount].provider = PROV_DXGI;
            sources[sourceCount].eventId = 42;
            sources[sourceCount].name = "Vulkan DXGI Present";
            ++sourceCount;
        }
        if (sourceCount < ETW_MAX_SOURCES)
        {
            sources[sourceCount].provider = PROV_DXGI;
            sources[sourceCount].eventId = 144;
            sources[sourceCount].name = "Vulkan DXGI MPO";
            ++sourceCount;
        }
        if (sourceCount < ETW_MAX_SOURCES)
        {
            sources[sourceCount].provider = PROV_DXGKRNL;
            sources[sourceCount].eventId = 168;
            sources[sourceCount].name = "Vulkan Flip";
            ++sourceCount;
        }
        break;

    default:
        break;
    }

    LeaveCriticalSection(&lock);

    log_bi::write("etw: auto-configured sources for %s", apiName(api));
}

void etw_bi::setTarget(DWORD processId)
{
    targetPid = processId;
}

void etw_bi::countEvent(DWORD pid, int providerIndex, unsigned eventId)
{
    if (censusStart100ns == 0 || pid != censusPid)
        return;

    unsigned key = ((unsigned)providerIndex << 16) | (eventId & 0xFFFF);

    EnterCriticalSection(&lock);
    if (censusStart100ns != 0 && pid == censusPid)
        ++census[key];
    LeaveCriticalSection(&lock);
}

void WINAPI etw_bi::eventCallback(PEVENT_RECORD record)
{
    etw_bi *self = (etw_bi *)record->UserContext;
    if (!self)
        return;

    DWORD pid = record->EventHeader.ProcessId;
    if (pid == 0 || pid == (DWORD)-1)
        return;

    const GUID &provider = record->EventHeader.ProviderId;
    USHORT id = record->EventHeader.EventDescriptor.Id;

    int providerIndex = -1;
    for (int i = 0; i < PROV_COUNT; ++i)
    {
        if (IsEqualGUID(provider, PROVIDER_GUIDS[i]))
        {
            providerIndex = i;
            break;
        }
    }

    if (providerIndex < 0)
        return;

    self->countEvent(pid, providerIndex, id);

    for (int i = 0; i < self->sourceCount; ++i)
    {
        if (self->sources[i].provider == providerIndex && self->sources[i].eventId == id)
        {
            self->onPresent(pid, i, record->EventHeader.TimeStamp.QuadPart);
            break;
        }
    }
}

void etw_bi::onPresent(DWORD pid, int sourceIndex, LONGLONG timestamp100ns)
{
    EnterCriticalSection(&lock);

    proc_source_bi &s = procs[pid].src[sourceIndex];

    ++s.presentCount;

    if (s.lastPresent100ns != 0 && timestamp100ns > s.lastPresent100ns)
    {
        LONGLONG delta = timestamp100ns - s.lastPresent100ns;

        s.intervalSum100ns += delta;
        ++s.intervalCount;

        if (pid == targetPid && sourceIndex == reportedSource)
        {
            if (frameOwnerPid != pid)
            {
                frameOwnerPid = pid;
                frameWritten = 0;
                frameDrained = 0;
                frameLost = 0;
            }

            frame_sample_bi &slot = frameRing[frameWritten % ETW_FRAME_RING];
            slot.time100ns = timestamp100ns;
            slot.intervalMs = (float)((double)delta / 10000.0);

            ++frameWritten;
        }
    }

    s.lastPresent100ns = timestamp100ns;

    LeaveCriticalSection(&lock);
}

size_t etw_bi::drainFrames(frame_sample_bi *out, size_t max)
{
    if (!out || max == 0)
        return 0;

    EnterCriticalSection(&lock);

    if (frameWritten > ETW_FRAME_RING)
    {
        unsigned long long oldest = frameWritten - ETW_FRAME_RING;
        if (frameDrained < oldest)
        {
            frameLost += oldest - frameDrained;
            frameDrained = oldest;
        }
    }

    unsigned long long available = frameWritten - frameDrained;
    size_t taken = (available < (unsigned long long)max) ? (size_t)available : max;

    for (size_t i = 0; i < taken; ++i)
        out[i] = frameRing[(frameDrained + i) % ETW_FRAME_RING];

    frameDrained += taken;

    LeaveCriticalSection(&lock);
    return taken;
}

DWORD WINAPI etw_bi::traceThread(LPVOID param)
{
    etw_bi *self = (etw_bi *)param;

    ULONG status = ProcessTrace(&self->consumerHandle, 1, NULL, NULL);

    if (self->sessionActive)
    {
        log_bi::writeErr(status, "etw: ProcessTrace returned while session was still active, "
                                 "restart needed");
        self->failed = true;
        self->sessionActive = false;
    }

    return 0;
}

bool etw_bi::start()
{
    if (sessionActive)
        return true;

    if (!isElevated)
    {
        failReason = "needs administrator rights";
        log_bi::write("etw: not elevated, frame metrics unavailable. "
                      "Enable 'Start as administrator' in Settings.");
        return false;
    }

    const size_t propsSize = sessionPropsSize();
    std::vector<char> buffer(propsSize, 0);
    EVENT_TRACE_PROPERTIES *props = (EVENT_TRACE_PROPERTIES *)&buffer[0];

    initSessionProps(props, propsSize);
    ControlTraceW(0, ETW_SESSION_NAME, props, EVENT_TRACE_CONTROL_STOP);

    initSessionProps(props, propsSize);

    props->BufferSize = 128;
    props->MinimumBuffers = 32;
    props->MaximumBuffers = 96;
    props->FlushTimer = 1;

    ULONG status = StartTraceW(&sessionHandle, ETW_SESSION_NAME, props);
    if (status != ERROR_SUCCESS)
    {
        sessionHandle = 0;
        failReason = "StartTrace failed";
        log_bi::writeErr(status, "etw: StartTraceW failed");
        return false;
    }

    for (int i = 0; i < PROV_COUNT; ++i)
    {
        ULONGLONG keyword = 0;

        if (!deepCensus)
        {
            if (i == PROV_DXGI)
                keyword = DXGI_KEYWORD_PRESENT;
            else if (i == PROV_D3D9)
                keyword = D3D9_KEYWORD_PRESENT;
            else
                keyword = DXGKRNL_KEYWORD_PRESENT;
        }

        ULONG r = EnableTraceEx2(sessionHandle, &PROVIDER_GUIDS[i],
                                 EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                                 TRACE_LEVEL_VERBOSE, keyword, 0, 0, NULL);
        if (r != ERROR_SUCCESS)
            log_bi::writeErr(r, "etw: EnableTraceEx2(%s) failed", providerName((provider_bi)i));
    }

    EVENT_TRACE_LOGFILEW logfile;
    ZeroMemory(&logfile, sizeof(logfile));
    logfile.LoggerName = (LPWSTR)ETW_SESSION_NAME;
    logfile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    logfile.EventRecordCallback = eventCallback;
    logfile.Context = this;

    consumerHandle = OpenTraceW(&logfile);
    if (consumerHandle == (TRACEHANDLE)INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        log_bi::writeErr(err, "etw: OpenTraceW failed");

        ControlTraceW(sessionHandle, NULL, props, EVENT_TRACE_CONTROL_STOP);
        sessionHandle = 0;
        consumerHandle = 0;
        failReason = "OpenTrace failed";
        return false;
    }

    sessionActive = true;
    failed = false;
    failReason = "";

    thread = CreateThread(NULL, 0, traceThread, this, 0, NULL);
    if (!thread)
    {
        log_bi::writeErr(GetLastError(), "etw: CreateThread for ProcessTrace failed");
        stop();
        failReason = "trace thread failed";
        return false;
    }

    log_bi::write("etw: session started, %u KB x %u buffers, %d present sources, "
                  "keywords %s",
                  (unsigned)props->BufferSize, (unsigned)props->MaximumBuffers,
                  sourceCount, deepCensus ? "all (deep census)" : "present only");
    return true;
}

void etw_bi::stop()
{
    if (!sessionActive && sessionHandle == 0)
        return;

    sessionActive = false;

    if (consumerHandle)
    {
        CloseTrace(consumerHandle);
        consumerHandle = 0;
    }

    if (sessionHandle)
    {
        const size_t propsSize = sessionPropsSize();
        std::vector<char> buffer(propsSize, 0);
        EVENT_TRACE_PROPERTIES *props = (EVENT_TRACE_PROPERTIES *)&buffer[0];

        props->Wnode.BufferSize = (ULONG)propsSize;
        props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

        ControlTraceW(sessionHandle, NULL, props, EVENT_TRACE_CONTROL_STOP);
        sessionHandle = 0;
    }

    if (thread)
    {
        WaitForSingleObject(thread, 3000);
        CloseHandle(thread);
        thread = NULL;
    }
}

void etw_bi::pollHealth(LONGLONG now)
{
    if (!sessionHandle)
        return;

    if (lastHealth100ns != 0 && (now - lastHealth100ns) < ETW_HEALTH_INTERVAL_100NS)
        return;

    lastHealth100ns = now;

    const size_t propsSize = sessionPropsSize();
    std::vector<char> buffer(propsSize, 0);
    EVENT_TRACE_PROPERTIES *props = (EVENT_TRACE_PROPERTIES *)&buffer[0];

    props->Wnode.BufferSize = (ULONG)propsSize;
    props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

    if (ControlTraceW(sessionHandle, NULL, props, EVENT_TRACE_CONTROL_QUERY) != ERROR_SUCCESS)
        return;

    if (props->EventsLost != lastEventsLost || props->RealTimeBuffersLost != lastBuffersLost)
    {
        log_bi::write("etw health: events lost %lu (+%lu), realtime buffers lost %lu, "
                      "buffers written %lu, tracked processes %u",
                      (unsigned long)props->EventsLost,
                      (unsigned long)(props->EventsLost - lastEventsLost),
                      (unsigned long)props->RealTimeBuffersLost,
                      (unsigned long)props->BuffersWritten,
                      (unsigned)procs.size());

        lastEventsLost = props->EventsLost;
        lastBuffersLost = props->RealTimeBuffersLost;
    }
}

void etw_bi::dumpCensus(double seconds)
{
    std::vector<std::pair<unsigned, unsigned> > entries;
    DWORD pid = censusPid;

    EnterCriticalSection(&lock);
    for (std::map<unsigned, unsigned>::const_iterator it = census.begin();
         it != census.end(); ++it)
    {
        entries.push_back(*it);
    }
    census.clear();
    LeaveCriticalSection(&lock);

    std::string name = resolveProcess(pid).name;

    if (entries.empty())
    {
        log_bi::write("etw census: pid %lu (%s) produced no graphics events at all",
                      (unsigned long)pid, name.c_str());
        return;
    }

    std::sort(entries.begin(), entries.end(),
              [](const std::pair<unsigned, unsigned> &a,
                 const std::pair<unsigned, unsigned> &b)
              { return a.second > b.second; });

    log_bi::write("etw census for pid %lu (%s) over %.1fs, find the row whose rate matches "
                  "your in-game FPS and put it into settings.ini as "
                  "etw.fallbackProvider / etw.fallbackEventId:",
                  (unsigned long)pid, name.c_str(), seconds);

    size_t shown = entries.size() < 20 ? entries.size() : 20;
    for (size_t i = 0; i < shown; ++i)
    {
        int prov = (int)(entries[i].first >> 16);
        unsigned id = entries[i].first & 0xFFFF;
        double rate = seconds > 0.01 ? (double)entries[i].second / seconds : 0.0;

        log_bi::write("  provider=%d (%-8s) id=%-5u count=%-7u rate=%.1f/s",
                      prov, providerName((provider_bi)prov), id, entries[i].second, rate);
    }
}

bool etw_bi::evaluateProcess(proc_stats_bi &p, LONGLONG now, sample_bi *out, int *sourceOut)
{
    bool any = false;

    for (int i = 0; i < sourceCount; ++i)
    {
        proc_source_bi &s = p.src[i];

        if (s.intervalCount > 0)
        {
            s.lastIntervalMs = ((double)s.intervalSum100ns / (double)s.intervalCount) / 10000.0;
            s.lastGood100ns = now;
        }

        s.intervalSum100ns = 0;
        s.intervalCount = 0;

        bool fresh = (s.lastGood100ns != 0) && ((now - s.lastGood100ns) < ETW_STALE_100NS);

        if (!any && fresh && s.lastIntervalMs > 0.0)
        {
            any = true;

            if (out)
            {
                out->frameIntervalMs = s.lastIntervalMs;
                out->fps = 1000.0 / s.lastIntervalMs;
                out->presents = s.presentCount;
                out->valid = true;
            }
            if (sourceOut)
                *sourceOut = i;
        }

        s.presentCount = 0;
    }

    if (any)
        p.lastActivity100ns = now;

    return any;
}

etw_bi::sample_bi etw_bi::sample()
{
    sample_bi result;

    if (!sessionActive)
        return result;

    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    LONGLONG now = ((LONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;

    DWORD wanted = targetPid;

    DWORD bestPid = 0;
    int bestSource = -1;
    sample_bi bestSample;

    DWORD stickyPid = 0;
    int stickySource = -1;
    sample_bi stickySample;

    int chosenSource = -1;

    std::vector<DWORD> unknownPids;

    EnterCriticalSection(&lock);

    for (std::map<DWORD, proc_stats_bi>::iterator it = procs.begin(); it != procs.end();)
    {
        sample_bi candidate;
        int source = -1;

        bool live = evaluateProcess(it->second, now, &candidate, &source);

        if (live && it->first == wanted)
        {
            result = candidate;
            result.pid = it->first;
            result.isForeground = true;
            chosenSource = source;
        }

        if (live && candidate.fps >= ETW_BACKGROUND_MIN_FPS)
        {
            const proc_ident_bi *ident = knownProcess(it->first);

            if (!ident)
            {
                unknownPids.push_back(it->first);
            }
            else if (!ident->excluded)
            {
                if (candidate.fps > bestSample.fps)
                {
                    bestPid = it->first;
                    bestSource = source;
                    bestSample = candidate;
                }

                if (it->first == reportedPid)
                {
                    stickyPid = it->first;
                    stickySource = source;
                    stickySample = candidate;
                }
            }
        }

        if (it->second.lastActivity100ns != 0 &&
            (now - it->second.lastActivity100ns) > ETW_PRUNE_100NS)
        {
            idents.erase(it->first);
            procs.erase(it++);
            continue;
        }

        ++it;
    }

    LeaveCriticalSection(&lock);

    for (size_t i = 0; i < unknownPids.size(); ++i)
        resolveProcess(unknownPids[i]);

    if (!result.valid && bestPid != 0)
    {
        if (stickyPid != 0 && stickySample.fps * ETW_SWITCH_MARGIN >= bestSample.fps)
        {
            result = stickySample;
            result.pid = stickyPid;
            chosenSource = stickySource;
        }
        else
        {
            result = bestSample;
            result.pid = bestPid;
            chosenSource = bestSource;
        }

        result.isForeground = false;
    }

    if (result.pid != reportedPid || chosenSource != reportedSource)
    {
        if (result.valid)
        {
            log_bi::write("etw: measuring pid %lu (%s) via %s at %.1f fps, %s",
                          (unsigned long)result.pid, resolveProcess(result.pid).name.c_str(),
                          sources[chosenSource].name, result.fps,
                          result.isForeground ? "foreground" : "background, highest fps");
        }
        else
        {
            log_bi::write("etw: no process is presenting frames");
        }

        reportedPid = result.pid;
        reportedSource = chosenSource;
    }

    pollHealth(now);

    if (censusStart100ns != 0)
    {
        if (now - censusStart100ns >= ETW_CENSUS_WINDOW_100NS)
        {
            double seconds = (double)(now - censusStart100ns) / 10000000.0;

            EnterCriticalSection(&lock);
            censusStart100ns = 0;
            LeaveCriticalSection(&lock);

            dumpCensus(seconds);
            lastCensusDump100ns = now;
        }
    }
    else if (!result.valid && wanted != 0 && censusRuns < ETW_CENSUS_MAX_RUNS)
    {
        bool cooledDown =
            (lastCensusDump100ns == 0) ||
            ((now - lastCensusDump100ns) > ETW_CENSUS_COOLDOWN_100NS);

        if (cooledDown)
        {
            EnterCriticalSection(&lock);
            census.clear();
            censusPid = wanted;
            censusStart100ns = now;
            ++censusRuns;
            LeaveCriticalSection(&lock);

            log_bi::write("etw: nothing presenting, censusing pid %lu (%s) for %d s",
                          (unsigned long)wanted, resolveProcess(wanted).name.c_str(),
                          (int)(ETW_CENSUS_WINDOW_100NS / 10000000LL));
        }
    }

    return result;
}
