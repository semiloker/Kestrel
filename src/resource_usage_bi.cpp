#include "resource_usage_bi.h"
#include "logger_bi.h"
#include "paths_bi.h"

#include <cctype>
#include <cwchar>
#include <format>

#include <wbemidl.h>

static void formatMegabytes(std::string &out, ULONGLONG bytes, int unit)
{
    bool useGb = (unit == resource_usage_bi::MEM_UNIT_GB) ||
                 (unit == resource_usage_bi::MEM_UNIT_AUTO &&
                  bytes >= (1024ULL * 1024ULL * 1024ULL));

    if (useGb)
        out = std::format("{:.2f} GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    else
        out = std::format("{} MB", (unsigned long long)(bytes / DIV));
}

resource_usage_bi::sampler_config_bi resource_usage_bi::captureSamplerConfig() const
{
    sampler_config_bi config;
    config.memUnit = memUnit;
    config.showCpuCores = cpuInfo.show_CoreUsagePercents;
    config.showRamTotalPhys = ramInfo.show_ullTotalPhys;
    config.showRamAvailPhys = ramInfo.show_ullAvailPhys;
    config.showRamTotalPageFile = ramInfo.show_ullTotalPageFile;
    config.showRamAvailPageFile = ramInfo.show_ullAvailPageFile;
    config.showRamTotalVirtual = ramInfo.show_ullTotalVirtual;
    config.showRamAvailVirtual = ramInfo.show_ullAvailVirtual;
    config.showRamAvailExtendedVirtual = ramInfo.show_ullAvailExtendedVirtual;
    return config;
}

bool resource_usage_bi::updateRam()
{
    return updateRamInto(ramInfo, captureSamplerConfig());
}

bool resource_usage_bi::updateRamInto(RamInfo &ram, const sampler_config_bi &config)
{
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);

    if (!GlobalMemoryStatusEx(&statex))
        return false;

    if (statex.ullTotalPhys > 0)
        ram.loadValue =
            (1.0 - (double)statex.ullAvailPhys / (double)statex.ullTotalPhys) * 100.0;
    else
        ram.loadValue = static_cast<double>(statex.dwMemoryLoad);

    if (statex.ullTotalPageFile > 0)
    {
        ram.commitValue =
            (1.0 - (double)statex.ullAvailPageFile / (double)statex.ullTotalPageFile) * 100.0;
    }

    ram.totalGB = static_cast<double>(statex.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0);
    ram.usedGB = static_cast<double>(statex.ullTotalPhys - statex.ullAvailPhys) /
                 (1024.0 * 1024.0 * 1024.0);

    ram.dwMemoryLoad = std::format("{}%", (unsigned long)statex.dwMemoryLoad);

    if (config.showRamTotalPhys)
        formatMegabytes(ram.ullTotalPhys, statex.ullTotalPhys, config.memUnit);
    if (config.showRamAvailPhys)
        formatMegabytes(ram.ullAvailPhys, statex.ullAvailPhys, config.memUnit);
    if (config.showRamTotalPageFile)
        formatMegabytes(ram.ullTotalPageFile, statex.ullTotalPageFile, config.memUnit);
    if (config.showRamAvailPageFile)
        formatMegabytes(ram.ullAvailPageFile, statex.ullAvailPageFile, config.memUnit);
    if (config.showRamTotalVirtual)
        formatMegabytes(ram.ullTotalVirtual, statex.ullTotalVirtual, config.memUnit);
    if (config.showRamAvailVirtual)
        formatMegabytes(ram.ullAvailVirtual, statex.ullAvailVirtual, config.memUnit);
    if (config.showRamAvailExtendedVirtual)
        formatMegabytes(ram.ullAvailExtendedVirtual, statex.ullAvailExtendedVirtual,
                        config.memUnit);

    return true;
}

bool resource_usage_bi::updateDisk()
{
    EnterCriticalSection(&sampleLock);
    bool ok = updateDiskInto(disksInfo);

    EnterCriticalSection(&publishLock);
    pubDisks = disksInfo;
    LeaveCriticalSection(&publishLock);

    LeaveCriticalSection(&sampleLock);
    return ok;
}

bool resource_usage_bi::updateDiskInto(std::vector<DiskInfo> &disks)
{
    DWORD drives = GetLogicalDrives();
    if (drives == 0)
        return false;

    std::vector<DiskInfo> sampledDisks;

    for (wchar_t drive = L'A'; drive <= L'Z'; ++drive)
    {
        DWORD mask = 1UL << (drive - L'A');
        if ((drives & mask) == 0)
            continue;

        std::wstring rootPath(1, drive);
        rootPath += L":\\";

        UINT driveType = GetDriveTypeW(rootPath.c_str());
        // The allow-list excludes mapped remote and media-only roots before
        // GetDiskFreeSpaceExW can block on an unavailable endpoint.
        if (driveType != DRIVE_FIXED &&
            driveType != DRIVE_REMOVABLE &&
            driveType != DRIVE_RAMDISK)
        {
            continue;
        }

        ULARGE_INTEGER freeBytes = {};
        ULARGE_INTEGER totalBytes = {};
        if (!GetDiskFreeSpaceExW(rootPath.c_str(), &freeBytes, &totalBytes, NULL) ||
            totalBytes.QuadPart == 0)
        {
            continue;
        }

        DiskInfo disk;
        disk.diskLetter = paths_bi::wideToUtf8(rootPath);
        disk.totalSpace = std::to_string(totalBytes.QuadPart / DIV) + " MB";
        disk.freeSpace = std::to_string(freeBytes.QuadPart / DIV) + " MB";
        disk.usedSpace =
            std::to_string((totalBytes.QuadPart - freeBytes.QuadPart) / DIV) + " MB";

        double usage =
            (1.0 - static_cast<double>(freeBytes.QuadPart) /
                       static_cast<double>(totalBytes.QuadPart)) *
            100.0;
        disk.usagePercent = std::format("{:.2f} %", usage);
        sampledDisks.push_back(disk);
    }

    disks.swap(sampledDisks);
    return !disks.empty();
}

static ULONGLONG octetDelta(DWORD current, DWORD previous)
{
    if (current >= previous)
        return (ULONGLONG)(current - previous);

    return (ULONGLONG)((0xFFFFFFFFULL - previous) + current + 1);
}

bool resource_usage_bi::updateNetwork()
{
    EnterCriticalSection(&sampleLock);
    bool ok = updateNetworkInto(networkInfo);

    EnterCriticalSection(&publishLock);
    pubNetwork = networkInfo;
    LeaveCriticalSection(&publishLock);

    LeaveCriticalSection(&sampleLock);
    return ok;
}

bool resource_usage_bi::updateNetworkInto(std::vector<NetworkInfo> &network)
{
    DWORD tableSize = 0;

    if (GetIfTable(NULL, &tableSize, FALSE) != ERROR_INSUFFICIENT_BUFFER ||
        tableSize == 0)
    {
        return false;
    }

    std::vector<BYTE> tableBuffer(tableSize);
    MIB_IFTABLE *ifTable = reinterpret_cast<MIB_IFTABLE *>(&tableBuffer[0]);

    if (GetIfTable(ifTable, &tableSize, TRUE) != NO_ERROR)
    {
        return false;
    }

    ULONGLONG now = GetTickCount64();
    double seconds = (netPrevTick != 0 && now > netPrevTick)
                         ? (double)(now - netPrevTick) / 1000.0
                         : 0.0;

    std::vector<NetworkInfo> sampledNetwork;

    for (DWORD i = 0; i < ifTable->dwNumEntries; ++i)
    {
        NetworkInfo netInfo;
        const MIB_IFROW &row = ifTable->table[i];

        size_t wlen = 0;
        while (wlen < MAX_INTERFACE_NAME_LEN && row.wszName[wlen] != L'\0')
            ++wlen;
        netInfo.interfaceName =
            paths_bi::wideToUtf8(std::wstring(row.wszName, wlen));

        NetCounters previous = {0, 0};
        std::map<DWORD, NetCounters>::const_iterator it = netPrev.find(row.dwIndex);
        bool havePrev = (it != netPrev.end());
        if (havePrev)
            previous = it->second;

        if (havePrev && seconds > 0.01)
        {
            double down = (double)octetDelta(row.dwInOctets, previous.in) / 1024.0 / seconds;
            double up = (double)octetDelta(row.dwOutOctets, previous.out) / 1024.0 / seconds;

            netInfo.downloadSpeed = std::format("{:.1f} KB/s", down);
            netInfo.uploadSpeed = std::format("{:.1f} KB/s", up);
            netInfo.downKBs = down;
            netInfo.upKBs = up;
            netInfo.speedValid = true;
        }
        else
        {
            netInfo.downloadSpeed = "-";
            netInfo.uploadSpeed = "-";
        }

        NetCounters current;
        current.in = row.dwInOctets;
        current.out = row.dwOutOctets;
        netPrev[row.dwIndex] = current;

        sampledNetwork.push_back(netInfo);
    }

    netPrevTick = now;
    network.swap(sampledNetwork);
    return true;
}

void resource_usage_bi::initCpuInfo()
{
    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        wchar_t cpuName[257] = {};
        DWORD size = sizeof(cpuName);
        DWORD type = 0;
        if (RegQueryValueExW(hKey, L"ProcessorNameString", NULL, &type,
                             reinterpret_cast<LPBYTE>(cpuName), &size) ==
                ERROR_SUCCESS &&
            (type == REG_SZ || type == REG_EXPAND_SZ))
        {
            cpuName[(sizeof(cpuName) / sizeof(cpuName[0])) - 1] = L'\0';
            cpuInfo.cpuName = paths_bi::wideToUtf8(cpuName);
        }
        RegCloseKey(hKey);
    }

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    cpuInfo.architecture =
        (sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) ? "x64" : (sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM) ? "ARM"
                                                                               : (sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_IA64)  ? "IA64"
                                                                               : (sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL) ? "x86"
                                                                                                                                                  : "Unknown";
}

bool resource_usage_bi::openSharedQuery()
{
    if (sharedQuery != NULL)
        return true;

    if (sharedQueryFailed)
        return false;

    if (PdhOpenQueryW(NULL, 0, &sharedQuery) != ERROR_SUCCESS)
    {
        sharedQuery = NULL;
        sharedQueryFailed = true;
        log_bi::write("pdh: PdhOpenQuery failed, all counter metrics disabled");
        return false;
    }

    if (PdhAddEnglishCounterW(sharedQuery, L"\\Processor(_Total)\\% Processor Time",
                              0, &cpuTotalCounter) != ERROR_SUCCESS)
    {
        cpuTotalCounter = NULL;
    }

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    cpuCoreCount = sysInfo.dwNumberOfProcessors;

    cpuCoreCounters.assign(cpuCoreCount, (PDH_HCOUNTER)NULL);

    for (DWORD i = 0; i < cpuCoreCount; ++i)
    {
        wchar_t path[64];
        swprintf(path, 64, L"\\Processor(%lu)\\%% Processor Time", (unsigned long)i);

        if (PdhAddEnglishCounterW(sharedQuery, path, 0, &cpuCoreCounters[i]) != ERROR_SUCCESS)
            cpuCoreCounters[i] = NULL;
    }

    if (PdhAddEnglishCounterW(sharedQuery, L"\\GPU Engine(*)\\Running Time",
                              0, &gpuRunningCounter) != ERROR_SUCCESS)
    {
        gpuRunningCounter = NULL;
        log_bi::write("pdh: no GPU Engine counter, gpu load and gpu ms disabled");
    }

    if (PdhAddEnglishCounterW(sharedQuery, L"\\GPU Adapter Memory(*)\\Dedicated Usage",
                              0, &vramCounter) != ERROR_SUCCESS)
    {
        vramCounter = NULL;
        log_bi::write("pdh: no GPU Adapter Memory counter, vram usage disabled");
    }

    PDH_STATUS powerAdded = PdhAddEnglishCounterW(sharedQuery, L"\\Energy Meter(*)\\Power",
                                                  0, &powerCounter);
    if (powerAdded != ERROR_SUCCESS)
        powerAdded = PdhAddCounterW(sharedQuery, L"\\Energy Meter(*)\\Power", 0, &powerCounter);

    if (powerAdded != ERROR_SUCCESS)
    {
        powerCounter = NULL;
        log_bi::write("cpu power: cannot add \\Energy Meter(*)\\Power, pdh status 0x%08lX",
                      (unsigned long)powerAdded);
    }

    PdhCollectQueryData(sharedQuery);
    return true;
}

bool resource_usage_bi::collectShared()
{
    sharedCollected = false;

    if (!openSharedQuery())
        return false;

    PDH_STATUS status = PdhCollectQueryData(sharedQuery);

    if (status != ERROR_SUCCESS)
        return false;

    sharedCollected = true;
    return true;
}

bool resource_usage_bi::updateCpu()
{
    EnterCriticalSection(&sampleLock);
    bool ok = updateCpuInto(cpuInfo, cpuInfo.show_CoreUsagePercents);
    LeaveCriticalSection(&sampleLock);
    return ok;
}

bool resource_usage_bi::updateCpuInto(CpuInfo &cpu, bool showCores)
{
    if (!sharedCollected || cpuTotalCounter == NULL)
        return false;

    PDH_FMT_COUNTERVALUE value;
    DWORD type = 0;

    if (PdhGetFormattedCounterValue(cpuTotalCounter, PDH_FMT_DOUBLE, &type, &value) != ERROR_SUCCESS)
        return false;

    if (value.CStatus != ERROR_SUCCESS)
        return false;

    cpu.UsageValue = value.doubleValue;

    cpu.UsagePercent = std::format("{:.2f}%", cpu.UsageValue);

    if (cpu.CoreUsagePercents.size() != cpuCoreCount)
        cpu.CoreUsagePercents.assign(cpuCoreCount, std::string("-"));

    if (!showCores)
        return true;

    for (DWORD i = 0; i < cpuCoreCount; ++i)
    {
        if (cpuCoreCounters[i] == NULL)
        {
            cpu.CoreUsagePercents[i] = "N/A";
            continue;
        }

        PDH_FMT_COUNTERVALUE core;
        if (PdhGetFormattedCounterValue(cpuCoreCounters[i], PDH_FMT_DOUBLE, &type, &core) ==
                ERROR_SUCCESS &&
            core.CStatus == ERROR_SUCCESS)
        {
            cpu.CoreUsagePercents[i] = std::format("{:.2f}%", core.doubleValue);
        }
        else
        {
            cpu.CoreUsagePercents[i] = "N/A";
        }
    }

    return true;
}

void resource_usage_bi::initGpuInfo()
{
    for (DWORD i = 0;; ++i)
    {
        DISPLAY_DEVICEW display = {};
        display.cb = sizeof(display);
        if (!EnumDisplayDevicesW(NULL, i, &display, 0))
            break;

        if (display.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
        {
            gpuInfo.gpuName = paths_bi::wideToUtf8(display.DeviceString);
            break;
        }
    }

    if (gpuInfo.gpuName.empty())
        gpuInfo.gpuName = "Unknown GPU";

    gpuInfo.gpuLoad = "0.00 %";

    IDXGIFactory1 *factory = NULL;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&factory)) && factory)
    {
        SIZE_T totalDedicated = 0;

        for (UINT i = 0;; ++i)
        {
            IDXGIAdapter1 *adapter = NULL;

            if (FAILED(factory->EnumAdapters1(i, &adapter)) || !adapter)
                break;

            DXGI_ADAPTER_DESC1 desc;
            if (SUCCEEDED(adapter->GetDesc1(&desc)))
            {
                bool software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;

                if (software)
                {
                    adapter->Release();
                    continue;
                }

                SIZE_T mem = desc.DedicatedVideoMemory;
                if (mem == 0)
                    mem = desc.DedicatedSystemMemory + desc.SharedSystemMemory;

                totalDedicated += mem;

                AdapterInfo ai;
                ai.adapterName = paths_bi::wideToUtf8(desc.Description);
                ai.totalVramMB = (double)mem / (1024.0 * 1024.0);
                adapters.push_back(ai);
            }

            adapter->Release();
        }

        if (totalDedicated > 0)
            gpuInfo.vramTotalMB = (double)totalDedicated / (1024.0 * 1024.0);

        factory->Release();
    }

    if (gpuInfo.vramTotalMB <= 0.0)
        log_bi::write("vram: no dedicated video memory reported by DXGI");
}

bool resource_usage_bi::updateGpu()
{
    return updateGpuTime(0, NULL);
}

bool resource_usage_bi::updateAll()
{
    bool success = updateRam();

    EnterCriticalSection(&sampleLock);
    bool collected = collectShared();
    success &= collected;
    if (collected)
    {
        success &= updateCpuInto(cpuInfo, cpuInfo.show_CoreUsagePercents);
        success &= updateGpuTimeInto(0, NULL, gpuInfo);
        updateCpuPowerInto(cpuInfo, gpuInfo);
    }
    LeaveCriticalSection(&sampleLock);

    updateTemps();
    success &= updateDisk();
    success &= updateNetwork();
    return success;
}

bool resource_usage_bi::updateHudSample()
{
    EnterCriticalSection(&sampleLock);

    if (!collectShared())
    {
        LeaveCriticalSection(&sampleLock);
        return false;
    }

    bool success = updateCpuInto(cpuInfo, cpuInfo.show_CoreUsagePercents);
    updateCpuPowerInto(cpuInfo, gpuInfo);
    updateGpuMemoryInto(gpuInfo, adapters);

    LeaveCriticalSection(&sampleLock);

    updateTemps();
    return success;
}

DWORD WINAPI resource_usage_bi::samplerEntry(LPVOID param)
{
    ((resource_usage_bi *)param)->samplerLoop();
    return 0;
}

void resource_usage_bi::samplerLoop()
{
    HRESULT comStatus = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool uninitializeCom = SUCCEEDED(comStatus);
    if (FAILED(comStatus))
    {
        log_bi::write("sampler: COM MTA initialization failed (0x%08lX); "
                      "WMI temperatures may be unavailable",
                      static_cast<unsigned long>(comStatus));
    }

    CpuInfo sampleCpu;
    RamInfo sampleRam;
    GpuInfo sampleGpu;
    std::vector<AdapterInfo> sampleAdapters;
    std::vector<DiskInfo> sampleDisks;
    std::vector<NetworkInfo> sampleNetwork;
    ULONGLONG lastSlowSampleTick = 0;

    EnterCriticalSection(&publishLock);
    sampleCpu = pubCpu;
    sampleRam = pubRam;
    sampleGpu = pubGpu;
    sampleAdapters = pubAdapters;
    sampleDisks = pubDisks;
    sampleNetwork = pubNetwork;
    LeaveCriticalSection(&publishLock);

    for (;;)
    {
        DWORD interval = (DWORD)samplerIntervalMs.load(std::memory_order_acquire);
        if (interval < 50)
            interval = 50;

        if (WaitForSingleObject(samplerStop, interval) != WAIT_TIMEOUT)
            break;

        sampler_config_bi config;
        EnterCriticalSection(&publishLock);
        config = samplerConfig;
        LeaveCriticalSection(&publishLock);

        EnterCriticalSection(&sampleLock);

        collectShared();
        updateCpuInto(sampleCpu, config.showCpuCores);
        updateCpuPowerInto(sampleCpu, sampleGpu);
        updateGpuMemoryInto(sampleGpu, sampleAdapters);
        updateRamInto(sampleRam, config);

        DWORD pid = samplerTargetPid.load(std::memory_order_acquire);

        double busyMs = 0.0;
        bool busyOk = updateGpuTimeInto(pid, &busyMs, sampleGpu);

        ULONGLONG slowSampleTick = GetTickCount64();
        if (lastSlowSampleTick == 0 ||
            slowSampleTick - lastSlowSampleTick >= 1000)
        {
            updateTempsInto(sampleCpu, sampleGpu);
            updateDiskInto(sampleDisks);
            updateNetworkInto(sampleNetwork);
            lastSlowSampleTick = GetTickCount64();
        }

        LeaveCriticalSection(&sampleLock);

        publishSample(sampleCpu, sampleRam, sampleGpu, sampleAdapters, sampleDisks,
                      sampleNetwork, busyMs, busyOk);
    }

    EnterCriticalSection(&sampleLock);
    if (thermalSvc != NULL)
    {
        thermalSvc->Release();
        thermalSvc = NULL;
    }
    LeaveCriticalSection(&sampleLock);

    if (uninitializeCom)
        CoUninitialize();
}

void resource_usage_bi::publishSample(const CpuInfo &cpu, const RamInfo &ram,
                                      const GpuInfo &gpu,
                                      const std::vector<AdapterInfo> &sampleAdapters,
                                      const std::vector<DiskInfo> &sampleDisks,
                                      const std::vector<NetworkInfo> &sampleNetwork,
                                      double gpuBusyMs, bool gpuBusyValid)
{
    if (!publishLockReady)
        return;

    EnterCriticalSection(&publishLock);

    pubCpu = cpu;
    pubRam = ram;
    pubGpu = gpu;
    pubAdapters = sampleAdapters;
    pubDisks = sampleDisks;
    pubNetwork = sampleNetwork;
    pubGpuBusyMs = gpuBusyMs;
    pubGpuBusyValid = gpuBusyValid;

    LeaveCriticalSection(&publishLock);
}

void resource_usage_bi::readSnapshot(CpuInfo *cpu, RamInfo *ram, GpuInfo *gpu,
                                     double *gpuBusyMs, bool *gpuBusyValid,
                                     std::vector<AdapterInfo> *adaptersOut)
{
    if (!publishLockReady)
        return;

    // UI-owned presentation settings are sampled only by the UI thread and
    // handed to the worker under the publication lock.
    sampler_config_bi config = captureSamplerConfig();
    CpuInfo sampledCpu;
    RamInfo sampledRam;
    GpuInfo sampledGpu;
    std::vector<AdapterInfo> sampledAdapters;
    std::vector<DiskInfo> sampledDisks;
    std::vector<NetworkInfo> sampledNetwork;
    double sampledBusyMs = 0.0;
    bool sampledBusyValid = false;

    EnterCriticalSection(&publishLock);
    samplerConfig = config;
    sampledCpu = pubCpu;
    sampledRam = pubRam;
    sampledGpu = pubGpu;
    sampledAdapters = pubAdapters;
    sampledDisks = pubDisks;
    sampledNetwork = pubNetwork;
    sampledBusyMs = pubGpuBusyMs;
    sampledBusyValid = pubGpuBusyValid;
    LeaveCriticalSection(&publishLock);

    // The worker never writes the public structures. Refresh measurement fields
    // here, on the UI thread, while preserving presentation settings.
    cpuInfo.UsagePercent = sampledCpu.UsagePercent;
    cpuInfo.CoreUsagePercents = sampledCpu.CoreUsagePercents;
    cpuInfo.UsageValue = sampledCpu.UsageValue;
    cpuInfo.packagePower = sampledCpu.packagePower;
    cpuInfo.packagePowerW = sampledCpu.packagePowerW;
    cpuInfo.packagePowerAvailable = sampledCpu.packagePowerAvailable;
    cpuInfo.cpuTempC = sampledCpu.cpuTempC;
    cpuInfo.cpuTempAvailable = sampledCpu.cpuTempAvailable;
    cpuInfo.cpuTempApproximate = sampledCpu.cpuTempApproximate;

    ramInfo.dwMemoryLoad = sampledRam.dwMemoryLoad;
    ramInfo.ullTotalPhys = sampledRam.ullTotalPhys;
    ramInfo.ullAvailPhys = sampledRam.ullAvailPhys;
    ramInfo.ullTotalPageFile = sampledRam.ullTotalPageFile;
    ramInfo.ullAvailPageFile = sampledRam.ullAvailPageFile;
    ramInfo.ullTotalVirtual = sampledRam.ullTotalVirtual;
    ramInfo.ullAvailVirtual = sampledRam.ullAvailVirtual;
    ramInfo.ullAvailExtendedVirtual = sampledRam.ullAvailExtendedVirtual;
    ramInfo.loadValue = sampledRam.loadValue;
    ramInfo.commitValue = sampledRam.commitValue;
    ramInfo.usedGB = sampledRam.usedGB;
    ramInfo.totalGB = sampledRam.totalGB;

    gpuInfo.gpuLoad = sampledGpu.gpuLoad;
    gpuInfo.gpuLoadValue = sampledGpu.gpuLoadValue;
    gpuInfo.vramUsedMB = sampledGpu.vramUsedMB;
    gpuInfo.vramTotalMB = sampledGpu.vramTotalMB;
    gpuInfo.vramAvailable = sampledGpu.vramAvailable;
    gpuInfo.gpuPower = sampledGpu.gpuPower;
    gpuInfo.gpuPowerW = sampledGpu.gpuPowerW;
    gpuInfo.gpuPowerAvailable = sampledGpu.gpuPowerAvailable;
    gpuInfo.gpuTempC = sampledGpu.gpuTempC;
    gpuInfo.gpuTempAvailable = sampledGpu.gpuTempAvailable;
    adapters = sampledAdapters;
    disksInfo = sampledDisks;
    networkInfo = sampledNetwork;

    if (cpu)
        *cpu = cpuInfo;
    if (ram)
        *ram = ramInfo;
    if (gpu)
        *gpu = gpuInfo;
    if (gpuBusyMs)
        *gpuBusyMs = sampledBusyMs;
    if (gpuBusyValid)
        *gpuBusyValid = sampledBusyValid;
    if (adaptersOut)
        *adaptersOut = adapters;
}

void resource_usage_bi::setSamplerInterval(int intervalMs)
{
    if (intervalMs < 50)
        intervalMs = 50;
    if (intervalMs > 5000)
        intervalMs = 5000;

    samplerIntervalMs.store((LONG)intervalMs, std::memory_order_release);
}

void resource_usage_bi::setSamplerTarget(DWORD pid)
{
    samplerTargetPid.store(pid, std::memory_order_release);
}

void resource_usage_bi::startSampler(int intervalMs)
{
    EnterCriticalSection(&samplerLifecycleLock);

    if (cleanupDone)
    {
        log_bi::write("sampler: cannot restart after resource cleanup");
        LeaveCriticalSection(&samplerLifecycleLock);
        return;
    }

    if (samplerThread)
    {
        LeaveCriticalSection(&samplerLifecycleLock);
        return;
    }

    setSamplerInterval(intervalMs);

    EnterCriticalSection(&publishLock);
    pubCpu = cpuInfo;
    pubRam = ramInfo;
    pubGpu = gpuInfo;
    pubAdapters = adapters;
    pubDisks = disksInfo;
    pubNetwork = networkInfo;
    samplerConfig = captureSamplerConfig();
    LeaveCriticalSection(&publishLock);

    samplerStop = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!samplerStop)
    {
        log_bi::write("sampler: could not create the stop event");
        LeaveCriticalSection(&samplerLifecycleLock);
        return;
    }

    samplerThread = CreateThread(NULL, 0, &resource_usage_bi::samplerEntry, this, 0, NULL);

    if (!samplerThread)
    {
        log_bi::write("sampler: could not start the worker thread");
        CloseHandle(samplerStop);
        samplerStop = NULL;
        LeaveCriticalSection(&samplerLifecycleLock);
        return;
    }

    SetThreadPriority(samplerThread, THREAD_PRIORITY_BELOW_NORMAL);

    log_bi::write("sampler: worker started at %d ms", intervalMs);
    LeaveCriticalSection(&samplerLifecycleLock);
}

void resource_usage_bi::stopSampler()
{
    EnterCriticalSection(&samplerLifecycleLock);

    if (!samplerThread)
    {
        LeaveCriticalSection(&samplerLifecycleLock);
        return;
    }

    if (samplerStop)
        SetEvent(samplerStop);

    DWORD waitResult = WaitForSingleObject(samplerThread, INFINITE);
    bool waitFailureLogged = false;
    while (waitResult != WAIT_OBJECT_0)
    {
        if (!waitFailureLogged)
        {
            log_bi::writeErr(GetLastError(),
                             "sampler: failed to confirm worker exit; retrying");
            waitFailureLogged = true;
        }
        Sleep(10);
        waitResult = WaitForSingleObject(samplerThread, INFINITE);
    }

    CloseHandle(samplerThread);
    samplerThread = NULL;

    if (samplerStop)
    {
        CloseHandle(samplerStop);
        samplerStop = NULL;
    }

    LeaveCriticalSection(&samplerLifecycleLock);
}

bool resource_usage_bi::updateGpuMemory()
{
    EnterCriticalSection(&sampleLock);
    bool ok = updateGpuMemoryInto(gpuInfo, adapters);
    LeaveCriticalSection(&sampleLock);
    return ok;
}

bool resource_usage_bi::updateGpuMemoryInto(
    GpuInfo &gpu, std::vector<AdapterInfo> &sampleAdapters)
{
    gpu.vramAvailable = false;
    for (auto &a : sampleAdapters)
        a.available = false;

    if (!sharedCollected || vramCounter == NULL)
        return false;

    DWORD bufferSize = 0;
    DWORD itemCount = 0;

    PDH_STATUS status = PdhGetFormattedCounterArrayW(vramCounter, PDH_FMT_LARGE,
                                                     &bufferSize, &itemCount, NULL);
    if (status != (PDH_STATUS)PDH_MORE_DATA || bufferSize == 0)
        return false;

    vramBuffer.resize(bufferSize);
    PDH_FMT_COUNTERVALUE_ITEM_W *items =
        reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(&vramBuffer[0]);

    if (PdhGetFormattedCounterArrayW(vramCounter, PDH_FMT_LARGE,
                                     &bufferSize, &itemCount, items) != ERROR_SUCCESS)
        return false;

    LONGLONG total = 0;

    if (itemCount > sampleAdapters.size())
        sampleAdapters.resize(itemCount);

    for (DWORD i = 0; i < itemCount; ++i)
    {
        if (items[i].FmtValue.CStatus != ERROR_SUCCESS)
            continue;

        LONGLONG val = items[i].FmtValue.largeValue;

        if (val > 0)
        {
            total += val;
            sampleAdapters[i].usedVramMB = (double)val / (1024.0 * 1024.0);
            sampleAdapters[i].available = true;

            if (sampleAdapters[i].adapterName.empty())
            {
                sampleAdapters[i].adapterName = paths_bi::wideToUtf8(
                    items[i].szName ? std::wstring(items[i].szName) : std::wstring());
            }
        }
    }

    if (total <= 0)
    {
        if (!vramLogged)
        {
            log_bi::write("vram: GPU Adapter Memory reported nothing across %lu instances",
                          (unsigned long)itemCount);
            vramLogged = true;
        }
        return false;
    }

    gpu.vramUsedMB = (double)total / (1024.0 * 1024.0);
    gpu.vramAvailable = true;

    return true;
}

bool resource_usage_bi::updateCpuPower()
{
    EnterCriticalSection(&sampleLock);
    bool ok = updateCpuPowerInto(cpuInfo, gpuInfo);
    LeaveCriticalSection(&sampleLock);
    return ok;
}

bool resource_usage_bi::updateCpuPowerInto(CpuInfo &cpu, GpuInfo &gpu)
{
    cpu.packagePowerAvailable = false;
    gpu.gpuPowerAvailable = false;

    if (!sharedCollected || powerCounter == NULL)
        return false;

    DWORD bufferSize = 0;
    DWORD itemCount = 0;

    PDH_STATUS status = PdhGetFormattedCounterArrayW(powerCounter, PDH_FMT_DOUBLE,
                                                     &bufferSize, &itemCount, NULL);
    if (status != (PDH_STATUS)PDH_MORE_DATA || bufferSize == 0)
    {
        ++powerAttempts;
        if (powerAttempts == 30)
            log_bi::write("cpu power: sizing call returned 0x%08lX, bytes %lu",
                          (unsigned long)status, (unsigned long)bufferSize);
        return false;
    }

    powerBuffer.resize(bufferSize);
    PDH_FMT_COUNTERVALUE_ITEM_W *items =
        reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(&powerBuffer[0]);

    if (PdhGetFormattedCounterArrayW(powerCounter, PDH_FMT_DOUBLE,
                                     &bufferSize, &itemCount, items) != ERROR_SUCCESS)
        return false;

    double packageMilliwatts = -1.0;
    double gpuMilliwatts = -1.0;
    std::string seen;

    for (DWORD i = 0; i < itemCount; ++i)
    {
        if (!items[i].szName)
            continue;

        char name[128];
        size_t n = 0;
        for (const wchar_t *p = items[i].szName; *p && n + 1 < sizeof(name); ++p)
            name[n++] = (char)tolower((int)(*p & 0xFF));
        name[n] = '\0';

        if (!powerLogged && seen.size() < 400)
        {
            if (!seen.empty())
                seen += ", ";
            seen += name;
        }

        if (items[i].FmtValue.CStatus != ERROR_SUCCESS)
            continue;

        bool isGpu = (strstr(name, "gpu") || strstr(name, "graphics")) && !strstr(name, "cpu");

        if (isGpu)
        {
            if (items[i].FmtValue.doubleValue > gpuMilliwatts)
                gpuMilliwatts = items[i].FmtValue.doubleValue;
            continue;
        }

        if (!strstr(name, "_pkg") && !strstr(name, "package"))
            continue;

        if (strstr(name, "core"))
            continue;

        if (items[i].FmtValue.doubleValue > packageMilliwatts)
            packageMilliwatts = items[i].FmtValue.doubleValue;
    }

    if (gpuMilliwatts >= 0.0)
    {
        gpu.gpuPowerW = gpuMilliwatts / 1000.0;
        gpu.gpuPowerAvailable = true;
        gpu.gpuPower = std::format("{:.2f} W", gpu.gpuPowerW);
    }

    if (packageMilliwatts < 0.0)
    {
        if (!powerLogged)
        {
            log_bi::write("cpu power: no package rail among %lu Energy Meter instances: %s",
                          (unsigned long)itemCount, seen.c_str());
            powerLogged = true;
        }
        return false;
    }

    if (!powerLogged)
    {
        log_bi::write("cpu power: package rail found, %.0f mW, instances: %s",
                      packageMilliwatts, seen.c_str());
        powerLogged = true;
    }

    cpu.packagePowerW = packageMilliwatts / 1000.0;
    cpu.packagePowerAvailable = true;
    cpu.packagePower = std::format("{:.2f} W", cpu.packagePowerW);

    return true;
}

// An ACPI zone that reports outside this band is reporting a placeholder, not a
// sensor. Kelvin readings of 0 and "critical trip point" constants are common.
static bool plausibleZoneTemp(double celsius)
{
    return celsius > 5.0 && celsius < 125.0;
}

// Zones are named after the ACPI path (\_TZ.TZ00, ACPI\ThermalZone\CPUZ_0, ...).
static bool zoneNamesCpu(const std::string &lowerName)
{
    return lowerName.find("cpu") != std::string::npos ||
           lowerName.find("proc") != std::string::npos;
}

bool resource_usage_bi::openTempQuery()
{
    if (tempQuery != NULL)
        return true;

    if (tempQueryFailed)
        return false;

    if (PdhOpenQueryW(NULL, 0, &tempQuery) != ERROR_SUCCESS)
    {
        tempQuery = NULL;
        tempQueryFailed = true;
        return false;
    }

    // High Precision Temperature is deci-Kelvin, Temperature is whole Kelvin.
    // Prefer the former and keep the latter for kernels that only expose it.
    if (PdhAddEnglishCounterW(tempQuery,
                              L"\\Thermal Zone Information(*)\\High Precision Temperature",
                              0, &thermalPreciseCounter) != ERROR_SUCCESS)
        thermalPreciseCounter = NULL;

    if (PdhAddEnglishCounterW(tempQuery, L"\\Thermal Zone Information(*)\\Temperature",
                              0, &thermalCounter) != ERROR_SUCCESS)
        thermalCounter = NULL;

    if (thermalPreciseCounter == NULL && thermalCounter == NULL)
    {
        PdhCloseQuery(tempQuery);
        tempQuery = NULL;
        tempQueryFailed = true;
        log_bi::write("temps: no Thermal Zone Information counters on this system");
        return false;
    }

    PdhCollectQueryData(tempQuery);
    return true;
}

bool resource_usage_bi::readThermalZonePdh(double *celsiusOut)
{
    if (!openTempQuery())
        return false;

    if (PdhCollectQueryData(tempQuery) != ERROR_SUCCESS)
        return false;

    // Two passes so a CPU-named zone always wins over a hotter chassis zone.
    double best = -1.0;
    double bestCpu = -1.0;
    std::string bestName;
    std::string bestCpuName;

    for (int pass = 0; pass < 2; ++pass)
    {
        PDH_HCOUNTER counter = (pass == 0) ? thermalPreciseCounter : thermalCounter;
        if (counter == NULL)
            continue;

        DWORD bufferSize = 0;
        DWORD itemCount = 0;

        if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bufferSize,
                                         &itemCount, NULL) != (PDH_STATUS)PDH_MORE_DATA ||
            bufferSize == 0)
            continue;

        thermalBuffer.resize(bufferSize);
        PDH_FMT_COUNTERVALUE_ITEM_W *items =
            reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(&thermalBuffer[0]);

        if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bufferSize,
                                         &itemCount, items) != ERROR_SUCCESS)
            continue;

        for (DWORD i = 0; i < itemCount; ++i)
        {
            if (items[i].FmtValue.CStatus != ERROR_SUCCESS)
                continue;

            double kelvin = (pass == 0) ? items[i].FmtValue.doubleValue / 10.0
                                        : items[i].FmtValue.doubleValue;
            double celsius = kelvin - 273.15;

            if (!plausibleZoneTemp(celsius))
                continue;

            std::string name = paths_bi::wideToUtf8(
                items[i].szName ? std::wstring(items[i].szName) : std::wstring());
            for (char &character : name)
            {
                character = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(character)));
            }

            if (zoneNamesCpu(name) && celsius > bestCpu)
            {
                bestCpu = celsius;
                bestCpuName = name;
            }
            else if (celsius > best)
            {
                best = celsius;
                bestName = name;
            }
        }

        // The precise counter covers every zone; no need to read the coarse one.
        if (bestCpu >= 0.0 || best >= 0.0)
            break;
    }

    double picked = (bestCpu >= 0.0) ? bestCpu : best;
    if (picked < 0.0)
        return false;

    if (!cpuTempLogged)
    {
        log_bi::write("temps: cpu from ACPI zone '%s' via pdh, %.1f C",
                      (bestCpu >= 0.0 ? bestCpuName : bestName).c_str(), picked);
        cpuTempLogged = true;
    }

    *celsiusOut = picked;
    return true;
}

bool resource_usage_bi::readThermalZoneWmi(double *celsiusOut)
{
    if (thermalWmiFailed)
        return false;

    if (thermalSvc == NULL)
    {
        // The sampler owns an MTA for its whole lifetime. Keep the proxy on
        // that worker instead of reconnecting for every slow sample.
        IWbemLocator *pLoc = NULL;
        if (FAILED(CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
                                    IID_IWbemLocator, (LPVOID *)&pLoc)) ||
            !pLoc)
        {
            thermalWmiFailed = true;
            return false;
        }

        BSTR ns = SysAllocString(L"ROOT\\WMI");
        HRESULT hres = pLoc->ConnectServer(ns, NULL, NULL, NULL, 0, NULL, NULL, &thermalSvc);
        SysFreeString(ns);
        pLoc->Release();

        if (FAILED(hres) || !thermalSvc)
        {
            thermalSvc = NULL;
            thermalWmiFailed = true;
            return false;
        }

        if (FAILED(CoSetProxyBlanket(thermalSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
                                     RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                                     NULL, EOAC_NONE)))
        {
            thermalSvc->Release();
            thermalSvc = NULL;
            thermalWmiFailed = true;
            return false;
        }
    }

    BSTR lang = SysAllocString(L"WQL");
    BSTR query = SysAllocString(L"SELECT InstanceName, CurrentTemperature "
                                L"FROM MSAcpi_ThermalZoneTemperature");
    IEnumWbemClassObject *pEnum = NULL;
    HRESULT hres = thermalSvc->ExecQuery(lang, query,
                                         WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                         NULL, &pEnum);
    SysFreeString(lang);
    SysFreeString(query);

    if (FAILED(hres) || !pEnum)
    {
        // The class is simply absent on most desktops; stop asking.
        thermalWmiFailed = true;
        return false;
    }

    double best = -1.0;
    double bestCpu = -1.0;
    std::string bestName;
    std::string bestCpuName;

    IWbemClassObject *pObj = NULL;
    ULONG ret = 0;
    ULONGLONG enumerationStarted = GetTickCount64();

    for (ULONG objectCount = 0;
         objectCount < 64 && GetTickCount64() - enumerationStarted < 500;
         ++objectCount)
    {
        pObj = NULL;
        ret = 0;
        HRESULT nextStatus = pEnum->Next(100, 1, &pObj, &ret);
        if (nextStatus != S_OK || ret != 1 || pObj == NULL)
        {
            if (pObj != NULL)
                pObj->Release();
            break;
        }

        VARIANT vt;
        VariantInit(&vt);

        if (SUCCEEDED(pObj->Get(L"CurrentTemperature", 0, &vt, 0, 0)) &&
            (vt.vt == VT_I4 || vt.vt == VT_UI4))
        {
            // Deci-Kelvin, 2732 == 0 C.
            double celsius = ((double)vt.lVal - 2732.0) / 10.0;

            if (plausibleZoneTemp(celsius))
            {
                VARIANT inst;
                VariantInit(&inst);
                std::string name;

                if (SUCCEEDED(pObj->Get(L"InstanceName", 0, &inst, 0, 0)) && inst.vt == VT_BSTR)
                {
                    name = paths_bi::wideToUtf8(
                        inst.bstrVal ? std::wstring(inst.bstrVal) : std::wstring());
                    for (char &character : name)
                    {
                        character = static_cast<char>(
                            std::tolower(static_cast<unsigned char>(character)));
                    }
                }
                VariantClear(&inst);

                if (zoneNamesCpu(name) && celsius > bestCpu)
                {
                    bestCpu = celsius;
                    bestCpuName = name;
                }
                else if (celsius > best)
                {
                    best = celsius;
                    bestName = name;
                }
            }
        }

        VariantClear(&vt);
        pObj->Release();
        pObj = NULL;
    }

    pEnum->Release();

    double picked = (bestCpu >= 0.0) ? bestCpu : best;
    if (picked < 0.0)
        return false;

    if (!cpuTempLogged)
    {
        log_bi::write("temps: cpu from ACPI zone '%s' via wmi, %.1f C",
                      (bestCpu >= 0.0 ? bestCpuName : bestName).c_str(), picked);
        cpuTempLogged = true;
    }

    *celsiusOut = picked;
    return true;
}

bool resource_usage_bi::updateTemps()
{
    EnterCriticalSection(&samplerLifecycleLock);
    if (samplerThread != NULL)
    {
        CpuInfo sampledCpu;
        GpuInfo sampledGpu;

        EnterCriticalSection(&publishLock);
        sampledCpu = pubCpu;
        sampledGpu = pubGpu;
        LeaveCriticalSection(&publishLock);
        LeaveCriticalSection(&samplerLifecycleLock);

        cpuInfo.cpuTempC = sampledCpu.cpuTempC;
        cpuInfo.cpuTempAvailable = sampledCpu.cpuTempAvailable;
        cpuInfo.cpuTempApproximate = sampledCpu.cpuTempApproximate;
        gpuInfo.gpuTempC = sampledGpu.gpuTempC;
        gpuInfo.gpuTempAvailable = sampledGpu.gpuTempAvailable;
        return cpuInfo.cpuTempAvailable || gpuInfo.gpuTempAvailable;
    }

    HRESULT comStatus = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    bool uninitializeCom = SUCCEEDED(comStatus);

    EnterCriticalSection(&sampleLock);
    bool ok = updateTempsInto(cpuInfo, gpuInfo);

    // The sampler keeps its proxy for repeated reads. A synchronous wrapper
    // must not leave a proxy tied to the caller's apartment after it returns.
    if (thermalSvc != NULL)
    {
        thermalSvc->Release();
        thermalSvc = NULL;
    }

    EnterCriticalSection(&publishLock);
    pubCpu.cpuTempC = cpuInfo.cpuTempC;
    pubCpu.cpuTempAvailable = cpuInfo.cpuTempAvailable;
    pubCpu.cpuTempApproximate = cpuInfo.cpuTempApproximate;
    pubGpu.gpuTempC = gpuInfo.gpuTempC;
    pubGpu.gpuTempAvailable = gpuInfo.gpuTempAvailable;
    LeaveCriticalSection(&publishLock);

    LeaveCriticalSection(&sampleLock);

    if (uninitializeCom)
        CoUninitialize();

    LeaveCriticalSection(&samplerLifecycleLock);
    return ok;
}

bool resource_usage_bi::updateTempsInto(CpuInfo &cpu, GpuInfo &gpuInfoSample)
{
    cpu.cpuTempAvailable = false;
    gpuInfoSample.gpuTempAvailable = false;

    gpu_sensor_bi::reading_bi gpuReading;
    if (gpuSensor.readTemperature(&gpuReading))
    {
        gpuInfoSample.gpuTempC = gpuReading.temperatureC;
        gpuInfoSample.gpuTempAvailable = true;

        if (!gpuTempLogged)
        {
            log_bi::write("temps: gpu from %s on '%s', %.1f C (limit %.0f C)",
                          gpuReading.source, gpuReading.adapterName.c_str(),
                          gpuReading.temperatureC, gpuReading.temperatureMaxC);
            gpuTempLogged = true;
        }
    }
    else if (!gpuTempLogged)
    {
        log_bi::write("temps: no gpu thermal sensor exposed by the driver");
        gpuTempLogged = true;
    }

    // Afterburner's block carries the real die sensor. Everything else falls
    // back to an ACPI zone, which on desktops tracks the board rather than the
    // package and can sit near room temperature under full load.
    double cpuCelsius = 0.0;
    if (mahmSensor.readCpuTemperature(&cpuCelsius))
    {
        cpu.cpuTempC = cpuCelsius;
        cpu.cpuTempAvailable = true;
        cpu.cpuTempApproximate = false;
    }
    else if (readThermalZonePdh(&cpuCelsius) || readThermalZoneWmi(&cpuCelsius))
    {
        cpu.cpuTempC = cpuCelsius;
        cpu.cpuTempAvailable = true;
        cpu.cpuTempApproximate = true;
    }
    else if (!cpuTempLogged)
    {
        log_bi::write("temps: no ACPI thermal zone exposes a cpu reading");
        cpuTempLogged = true;
    }

    return cpu.cpuTempAvailable || gpuInfoSample.gpuTempAvailable;
}

bool resource_usage_bi::updateGpuTime(DWORD pid, double *busyMsOut)
{
    EnterCriticalSection(&sampleLock);
    bool ok = updateGpuTimeInto(pid, busyMsOut, gpuInfo);
    LeaveCriticalSection(&sampleLock);
    return ok;
}

bool resource_usage_bi::updateGpuTimeInto(DWORD pid, double *busyMsOut, GpuInfo &gpu)
{
    if (busyMsOut)
        *busyMsOut = 0.0;

    if (!sharedCollected || gpuRunningCounter == NULL)
        return false;

    if (pid != gpuTimePid)
    {
        gpuTimePid = pid;
        gpuTimeMisses = 0;
    }

    DWORD bufferSize = 0;
    DWORD itemCount = 0;

    PDH_STATUS status = PdhGetRawCounterArrayW(gpuRunningCounter, &bufferSize, &itemCount, NULL);
    if (status != (PDH_STATUS)PDH_MORE_DATA || bufferSize == 0)
        return false;

    gpuBuffer.resize(bufferSize);
    PDH_RAW_COUNTER_ITEM_W *items =
        reinterpret_cast<PDH_RAW_COUNTER_ITEM_W *>(&gpuBuffer[0]);

    if (PdhGetRawCounterArrayW(gpuRunningCounter, &bufferSize, &itemCount, items) != ERROR_SUCCESS)
        return false;

    wchar_t prefix[32];
    int prefixLen = swprintf(prefix, 32, L"pid_%lu_", (unsigned long)pid);
    if (prefixLen < 0)
        prefixLen = 0;

    ULONGLONG pidDelta100ns = 0;
    ULONGLONG totalDelta100ns = 0;
    int matched = 0;

    for (DWORD i = 0; i < itemCount; ++i)
    {
        if (!items[i].szName)
            continue;

        const wchar_t *name = items[i].szName;

        if (!wcsstr(name, L"engtype_3D"))
            continue;

        ULONGLONG current = (ULONGLONG)items[i].RawValue.FirstValue;

        std::map<std::wstring, ULONGLONG>::iterator prev = gpuTimePrev.find(name);

        ULONGLONG delta = 0;
        if (prev != gpuTimePrev.end())
        {
            if (current > prev->second)
                delta = current - prev->second;

            prev->second = current;
        }
        else
        {
            gpuTimePrev.insert(std::make_pair(std::wstring(name), current));
        }

        totalDelta100ns += delta;

        if (pid != 0 && prefixLen > 0 && wcsncmp(name, prefix, (size_t)prefixLen) == 0)
        {
            ++matched;
            pidDelta100ns += delta;
        }
    }

    if (gpuTimePrev.size() > 1024)
        gpuTimePrev.clear();

    ULONGLONG now = GetTickCount64();
    double elapsedMs = (gpuLastTick != 0 && now > gpuLastTick) ? (double)(now - gpuLastTick) : 0.0;
    gpuLastTick = now;

    if (elapsedMs > 0.5)
    {
        double busyMs = (double)totalDelta100ns / 10000.0;
        double load = busyMs / elapsedMs * 100.0;

        if (load < 0.0)
            load = 0.0;
        if (load > 100.0)
            load = 100.0;

        gpu.gpuLoadValue = load;
        gpu.gpuLoad = std::format("{:.2f}%", load);
    }

    if (pid == 0)
        return true;

    if (matched == 0)
    {
        ++gpuTimeMisses;

        if (gpuTimeMisses == 40)
        {
            log_bi::write("gpu time: no 3D engine instance for pid %lu among %lu counters",
                          (unsigned long)pid, (unsigned long)itemCount);
        }

        return false;
    }

    if (gpuTimeMisses != 0)
    {
        log_bi::write("gpu time: tracking %d 3D engine instance(s) for pid %lu",
                      matched, (unsigned long)pid);
        gpuTimeMisses = 0;
    }

    if (busyMsOut)
        *busyMsOut = (double)pidDelta100ns / 10000.0;

    return true;
}

void resource_usage_bi::cleanup()
{
    EnterCriticalSection(&samplerLifecycleLock);
    if (cleanupDone)
    {
        LeaveCriticalSection(&samplerLifecycleLock);
        return;
    }
    cleanupDone = true;

    // The critical section is recursive, so stopSampler can join the worker
    // while preventing a concurrent restart between the join and cleanup.
    stopSampler();
    LeaveCriticalSection(&samplerLifecycleLock);

    EnterCriticalSection(&sampleLock);

    if (sharedQuery != NULL)
    {
        PdhCloseQuery(sharedQuery);
        sharedQuery = NULL;
    }

    if (tempQuery != NULL)
    {
        PdhCloseQuery(tempQuery);
        tempQuery = NULL;
    }

    thermalPreciseCounter = NULL;
    thermalCounter = NULL;
    thermalBuffer.clear();

    cpuTotalCounter = NULL;
    cpuCoreCounters.clear();
    gpuRunningCounter = NULL;
    powerCounter = NULL;
    vramCounter = NULL;
    sharedCollected = false;

    gpuTimePrev.clear();
    gpuBuffer.clear();
    powerBuffer.clear();
    vramBuffer.clear();

    LeaveCriticalSection(&sampleLock);

    networkInfo.clear();
    disksInfo.clear();
}

bool resource_usage_bi::isStartWithWindowsEnabled()
{
    return autostart_bi::current() != autostart_bi::AUTOSTART_OFF;
}

bool resource_usage_bi::isStartAsAdminEnabled()
{
    return autostart_bi::current() == autostart_bi::AUTOSTART_ADMIN;
}

bool resource_usage_bi::enableStartWithWindows()
{
    autostart_bi::mode_bi mode = start_As_Admin ? autostart_bi::AUTOSTART_ADMIN
                                                : autostart_bi::AUTOSTART_NORMAL;

    bool ok = autostart_bi::setMode(mode);

    start_With_Windows = isStartWithWindowsEnabled();
    start_As_Admin = isStartAsAdminEnabled();
    return ok;
}

bool resource_usage_bi::disableStartWithWindows()
{
    bool ok = autostart_bi::setMode(autostart_bi::AUTOSTART_OFF);

    start_With_Windows = isStartWithWindowsEnabled();
    start_As_Admin = isStartAsAdminEnabled();
    return ok;
}

bool resource_usage_bi::toggleStartWithWindows()
{
    if (isStartWithWindowsEnabled())
    {
        return disableStartWithWindows();
    }
    else
    {
        return enableStartWithWindows();
    }
}

bool resource_usage_bi::toggleStartAsAdmin()
{
    bool wantAdmin = !isStartAsAdminEnabled();

    autostart_bi::mode_bi mode;
    if (wantAdmin)
        mode = autostart_bi::AUTOSTART_ADMIN;
    else
        mode = start_With_Windows ? autostart_bi::AUTOSTART_NORMAL
                                  : autostart_bi::AUTOSTART_OFF;

    bool ok = autostart_bi::setMode(mode);

    start_As_Admin = isStartAsAdminEnabled();
    start_With_Windows = isStartWithWindowsEnabled();
    return ok;
}
