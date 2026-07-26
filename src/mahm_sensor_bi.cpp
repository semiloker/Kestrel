#include "mahm_sensor_bi.h"
#include "logger_bi.h"

#include <cstdint>
#include <cstring>
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
        viewSize_ = 0;
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

    MEMORY_BASIC_INFORMATION region = {};
    if (VirtualQuery(view_, &region, sizeof(region)) != sizeof(region) ||
        region.State != MEM_COMMIT || region.RegionSize < sizeof(mahm_header_bi))
    {
        closeMapping();
        return false;
    }

    uintptr_t viewAddress = reinterpret_cast<uintptr_t>(view_);
    uintptr_t regionAddress = reinterpret_cast<uintptr_t>(region.BaseAddress);
    if (viewAddress < regionAddress)
    {
        closeMapping();
        return false;
    }
    size_t offset = static_cast<size_t>(viewAddress - regionAddress);
    if (offset >= region.RegionSize)
    {
        closeMapping();
        return false;
    }
    viewSize_ = region.RegionSize - offset;

    return true;
}

bool mahm_sensor_bi::readCpuTemperature(double *celsiusOut)
{
    if (!celsiusOut)
        return false;

    if (!ensureMapping())
        return false;

    if (viewSize_ < sizeof(mahm_header_bi))
    {
        closeMapping();
        return false;
    }

    mahm_header_bi header = {};
    std::memcpy(&header, view_, sizeof(header));
    const mahm_header_bi *h = &header;

    if (h->dwSignature == MAHM_SIGNATURE_DEAD)
        return false;  // monitoring disabled in Afterburner, keep the mapping

    if (h->dwSignature != MAHM_SIGNATURE || h->dwVersion < MAHM_MIN_VERSION ||
        h->dwHeaderSize < sizeof(mahm_header_bi) ||
        h->dwEntrySize < MAHM_STRING_BLOCK + MAHM_PAYLOAD ||
        h->dwHeaderSize > viewSize_ ||
        h->dwNumEntries > (viewSize_ - h->dwHeaderSize) / h->dwEntrySize)
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

        float data = 0.0f;
        DWORD gpu = 0;
        DWORD srcId = 0;
        std::memcpy(&data, entry + MAHM_STRING_BLOCK, sizeof(data));
        std::memcpy(&gpu,
                    entry + MAHM_STRING_BLOCK + 3 * sizeof(float) + sizeof(DWORD),
                    sizeof(gpu));
        std::memcpy(&srcId,
                    entry + MAHM_STRING_BLOCK + 3 * sizeof(float) + 2 * sizeof(DWORD),
                    sizeof(srcId));

        if (srcId != MAHM_SRC_CPU_TEMPERATURE)
            continue;

        double celsius = (double)data;
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
