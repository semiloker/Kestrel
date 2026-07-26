#include "mahm_sensor_bi.h"
#include "logger_bi.h"

#include <ctime>

namespace
{

// Layout published by MSI Afterburner's hardware monitor. Field offsets are
// read through the header's own dwHeaderSize / dwEntrySize rather than assumed,
// so a newer build that appends fields still parses.
#pragma pack(push, 1)
struct mahm_header_bi
{
    DWORD dwSignature;
    DWORD dwVersion;
    DWORD dwHeaderSize;
    DWORD dwNumEntries;
    DWORD dwEntrySize;
    int time;  // unix timestamp of the last refresh
    DWORD dwNumGpuEntries;
    DWORD dwGpuEntrySize;
};
#pragma pack(pop)

// 'MAHM' as it sits in memory.
const DWORD MAHM_SIGNATURE = 0x4D41484D;
// Afterburner stamps this when hardware monitoring is switched off.
const DWORD MAHM_SIGNATURE_DEAD = 0xDEAD;

const DWORD MAHM_MIN_VERSION = 0x00020000;

// Each entry starts with five fixed-size strings, then the numeric payload.
const DWORD MAHM_STRING_BLOCK = 5 * MAX_PATH;
const DWORD MAHM_PAYLOAD = 3 * sizeof(float) + 3 * sizeof(DWORD);

// Source ids are stable across versions and language independent.
const DWORD MAHM_SRC_CPU_TEMPERATURE = 0x80;
// Marks the aggregate reading rather than a per-core one.
const DWORD MAHM_GPU_UNASSIGNED = 0xFFFFFFFF;

// Afterburner refreshes about once a second; anything older means it stopped
// updating while the section is still mapped.
const int MAHM_STALE_SECONDS = 10;

const DWORD MAHM_REOPEN_INTERVAL_MS = 5000;

}  // namespace

mahm_sensor_bi::~mahm_sensor_bi()
{
    closeMapping();
}

void mahm_sensor_bi::closeMapping()
{
    if (view_)
    {
        UnmapViewOfFile(view_);
        view_ = NULL;
    }

    if (map_)
    {
        CloseHandle(map_);
        map_ = NULL;
    }
}

bool mahm_sensor_bi::ensureMapping()
{
    if (view_)
        return true;

    // Afterburner may not be running; do not hammer the object manager.
    DWORD now = GetTickCount();
    if (lastOpenAttempt_ != 0 && (now - lastOpenAttempt_) < MAHM_REOPEN_INTERVAL_MS)
        return false;

    lastOpenAttempt_ = now;

    map_ = OpenFileMappingA(FILE_MAP_READ, FALSE, "MAHMSharedMemory");
    if (!map_)
        map_ = OpenFileMappingA(FILE_MAP_READ, FALSE, "Global\\MAHMSharedMemory");

    if (!map_)
        return false;

    view_ = (const BYTE *)MapViewOfFile(map_, FILE_MAP_READ, 0, 0, 0);
    if (!view_)
    {
        CloseHandle(map_);
        map_ = NULL;
        return false;
    }

    return true;
}

bool mahm_sensor_bi::readCpuTemperature(double *celsiusOut)
{
    if (!celsiusOut)
        return false;

    if (!ensureMapping())
        return false;

    const mahm_header_bi *h = (const mahm_header_bi *)view_;

    if (h->dwSignature == MAHM_SIGNATURE_DEAD)
        return false;  // monitoring disabled in Afterburner, keep the mapping

    if (h->dwSignature != MAHM_SIGNATURE || h->dwVersion < MAHM_MIN_VERSION ||
        h->dwHeaderSize < sizeof(mahm_header_bi) ||
        h->dwEntrySize < MAHM_STRING_BLOCK + MAHM_PAYLOAD)
    {
        closeMapping();
        return false;
    }

    // Afterburner exited but something still holds the section alive: the
    // numbers are frozen, so drop it and let the caller fall back.
    if ((long long)time(NULL) - (long long)h->time > MAHM_STALE_SECONDS)
    {
        closeMapping();
        return false;
    }

    double aggregate = -1.0;
    double hottestCore = -1.0;

    for (DWORD i = 0; i < h->dwNumEntries; ++i)
    {
        const BYTE *entry = view_ + h->dwHeaderSize + (size_t)i * h->dwEntrySize;

        const float *data = (const float *)(entry + MAHM_STRING_BLOCK);
        const DWORD *tail = (const DWORD *)(entry + MAHM_STRING_BLOCK + 3 * sizeof(float));

        DWORD gpu = tail[1];
        DWORD srcId = tail[2];

        if (srcId != MAHM_SRC_CPU_TEMPERATURE)
            continue;

        double celsius = (double)*data;
        if (celsius <= 0.0 || celsius >= 150.0)
            continue;

        if (gpu == MAHM_GPU_UNASSIGNED)
        {
            if (celsius > aggregate)
                aggregate = celsius;
        }
        else if (celsius > hottestCore)
        {
            hottestCore = celsius;
        }
    }

    // Prefer Afterburner's own package figure; otherwise the hottest core is
    // the number every other monitor reports as "CPU temperature".
    double picked = (aggregate >= 0.0) ? aggregate : hottestCore;
    if (picked < 0.0)
        return false;

    if (!logged_)
    {
        log_bi::write("temps: cpu from MSI Afterburner shared memory, %.1f C", picked);
        logged_ = true;
    }

    *celsiusOut = picked;
    return true;
}
