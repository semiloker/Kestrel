#include "resource_usage_bi.h"
#include "logger_bi.h"

#include <cctype>
#include <cstring>
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

bool resource_usage_bi::updateRam()
{
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);

    if (!GlobalMemoryStatusEx(&statex))
        return false;

    if (statex.ullTotalPhys > 0)
        ramInfo.loadValue =
            (1.0 - (double)statex.ullAvailPhys / (double)statex.ullTotalPhys) * 100.0;
    else
        ramInfo.loadValue = static_cast<double>(statex.dwMemoryLoad);

    if (statex.ullTotalPageFile > 0)
    {
        ramInfo.commitValue =
            (1.0 - (double)statex.ullAvailPageFile / (double)statex.ullTotalPageFile) * 100.0;
    }

    ramInfo.totalGB = static_cast<double>(statex.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0);
    ramInfo.usedGB = static_cast<double>(statex.ullTotalPhys - statex.ullAvailPhys) /
                     (1024.0 * 1024.0 * 1024.0);

    ramInfo.dwMemoryLoad = std::format("{}%", (unsigned long)statex.dwMemoryLoad);

    if (ramInfo.show_ullTotalPhys)
        formatMegabytes(ramInfo.ullTotalPhys, statex.ullTotalPhys, memUnit);
    if (ramInfo.show_ullAvailPhys)
        formatMegabytes(ramInfo.ullAvailPhys, statex.ullAvailPhys, memUnit);
    if (ramInfo.show_ullTotalPageFile)
        formatMegabytes(ramInfo.ullTotalPageFile, statex.ullTotalPageFile, memUnit);
    if (ramInfo.show_ullAvailPageFile)
        formatMegabytes(ramInfo.ullAvailPageFile, statex.ullAvailPageFile, memUnit);
    if (ramInfo.show_ullTotalVirtual)
        formatMegabytes(ramInfo.ullTotalVirtual, statex.ullTotalVirtual, memUnit);
    if (ramInfo.show_ullAvailVirtual)
        formatMegabytes(ramInfo.ullAvailVirtual, statex.ullAvailVirtual, memUnit);
    if (ramInfo.show_ullAvailExtendedVirtual)
        formatMegabytes(ramInfo.ullAvailExtendedVirtual, statex.ullAvailExtendedVirtual, memUnit);

    return true;
}

bool resource_usage_bi::updateDisk()
{
    DWORD drives = GetLogicalDrives();
    disksInfo.clear();

    for (char drive = 'A'; drive <= 'Z'; ++drive)
    {
        if (drives & (1 << (drive - 'A')))
        {
            std::string rootPath = std::string(1, drive) + ":\\";
            DiskInfo disk;
            disk.diskLetter = rootPath;

            ULARGE_INTEGER freeBytes, totalBytes, totalFreeBytes;
            if (GetDiskFreeSpaceExA(rootPath.c_str(), &freeBytes, &totalBytes, &totalFreeBytes))
            {
                disk.totalSpace = std::to_string(totalBytes.QuadPart / DIV) + " MB";
                disk.freeSpace = std::to_string(freeBytes.QuadPart / DIV) + " MB";
                disk.usedSpace = std::to_string((totalBytes.QuadPart - freeBytes.QuadPart) / DIV) + " MB";

                double usage = (1.0 - (static_cast<double>(freeBytes.QuadPart) / totalBytes.QuadPart)) * 100;
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(2) << usage << " %";
                disk.usagePercent = oss.str();

                disksInfo.push_back(disk);
            }
        }
    }
    return !disksInfo.empty();
}

static ULONGLONG octetDelta(DWORD current, DWORD previous)
{
    if (current >= previous)
        return (ULONGLONG)(current - previous);

    return (ULONGLONG)((0xFFFFFFFFULL - previous) + current + 1);
}

bool resource_usage_bi::updateNetwork()
{
    MIB_IFTABLE *pIfTable = NULL;
    DWORD dwSize = 0;

    if (GetIfTable(NULL, &dwSize, FALSE) != ERROR_INSUFFICIENT_BUFFER || dwSize == 0)
        return false;

    pIfTable = (MIB_IFTABLE *)malloc(dwSize);
    if (!pIfTable)
        return false;

    if (GetIfTable(pIfTable, &dwSize, TRUE) != NO_ERROR)
    {
        free(pIfTable);
        return false;
    }

    ULONGLONG now = GetTickCount64();
    double seconds = (netPrevTick != 0 && now > netPrevTick)
                         ? (double)(now - netPrevTick) / 1000.0
                         : 0.0;

    networkInfo.clear();

    for (DWORD i = 0; i < pIfTable->dwNumEntries; ++i)
    {
        NetworkInfo netInfo;
        const MIB_IFROW &row = pIfTable->table[i];

        wchar_t wname[MAX_INTERFACE_NAME_LEN + 1];
        size_t wlen = 0;
        while (wlen < MAX_INTERFACE_NAME_LEN && row.wszName[wlen] != L'\0')
            ++wlen;
        memcpy(wname, row.wszName, wlen * sizeof(wchar_t));
        wname[wlen] = L'\0';

        char name[(MAX_INTERFACE_NAME_LEN + 1) * 2] = {0};
        WideCharToMultiByte(CP_ACP, 0, wname, -1, name, sizeof(name) - 1, NULL, NULL);
        netInfo.interfaceName = name;

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

        networkInfo.push_back(netInfo);
    }

    netPrevTick = now;
    free(pIfTable);
    return true;
}

void resource_usage_bi::initCpuInfo()
{
    HKEY hKey;
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE,
                     "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                     0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {

        char cpuName[257] = {0};
        DWORD size = sizeof(cpuName) - 1;
        if (RegQueryValueEx(hKey, "ProcessorNameString", NULL, NULL,
                            (LPBYTE)cpuName, &size) == ERROR_SUCCESS)
        {
            cpuName[sizeof(cpuName) - 1] = '\0';
            cpuInfo.cpuName = cpuName;
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
    cpuInfo.CoreUsagePercents.assign(cpuCoreCount, std::string("-"));

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
    if (!sharedCollected || cpuTotalCounter == NULL)
        return false;

    PDH_FMT_COUNTERVALUE value;
    DWORD type = 0;

    if (PdhGetFormattedCounterValue(cpuTotalCounter, PDH_FMT_DOUBLE, &type, &value) != ERROR_SUCCESS)
        return false;

    if (value.CStatus != ERROR_SUCCESS)
        return false;

    cpuInfo.UsageValue = value.doubleValue;

    cpuInfo.UsagePercent = std::format("{:.2f}%", cpuInfo.UsageValue);

    if (!cpuInfo.show_CoreUsagePercents)
        return true;

    for (DWORD i = 0; i < cpuCoreCount; ++i)
    {
        if (cpuCoreCounters[i] == NULL)
        {
            cpuInfo.CoreUsagePercents[i] = "N/A";
            continue;
        }

        PDH_FMT_COUNTERVALUE core;
        if (PdhGetFormattedCounterValue(cpuCoreCounters[i], PDH_FMT_DOUBLE, &type, &core) ==
                ERROR_SUCCESS &&
            core.CStatus == ERROR_SUCCESS)
        {
            cpuInfo.CoreUsagePercents[i] = std::format("{:.2f}%", core.doubleValue);
        }
        else
        {
            cpuInfo.CoreUsagePercents[i] = "N/A";
        }
    }

    return true;
}

void resource_usage_bi::initGpuInfo()
{
    DISPLAY_DEVICEA dd;
    ZeroMemory(&dd, sizeof(dd));
    dd.cb = sizeof(dd);

    for (DWORD i = 0; EnumDisplayDevicesA(NULL, i, &dd, 0); ++i)
    {
        if (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
        {
            gpuInfo.gpuName = dd.DeviceString;
            break;
        }
        ZeroMemory(&dd, sizeof(dd));
        dd.cb = sizeof(dd);
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
                char nameBuf[256];
                int len = WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1,
                                              nameBuf, sizeof(nameBuf), NULL, NULL);
                if (len > 0)
                    ai.adapterName = nameBuf;
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

DWORD WINAPI resource_usage_bi::samplerEntry(LPVOID param)
{
    ((resource_usage_bi *)param)->samplerLoop();
    return 0;
}

void resource_usage_bi::samplerLoop()
{
    for (;;)
    {
        DWORD interval = (DWORD)InterlockedCompareExchange(&samplerIntervalMs, 0, 0);
        if (interval < 50)
            interval = 50;

        if (WaitForSingleObject(samplerStop, interval) != WAIT_TIMEOUT)
            return;

        if (!collectShared())
            continue;

        updateCpu();
        updateCpuPower();
        updateGpuMemory();
        updateRam();

        DWORD pid = (DWORD)InterlockedCompareExchange(&samplerTargetPid, 0, 0);

        double busyMs = 0.0;
        bool busyOk = updateGpuTime(pid, &busyMs);

        publishSample(busyMs, busyOk);

    }
}

void resource_usage_bi::publishSample(double gpuBusyMs, bool gpuBusyValid)
{
    if (!publishLockReady)
        return;

    EnterCriticalSection(&publishLock);

    pubCpu = cpuInfo;
    pubRam = ramInfo;
    pubGpu = gpuInfo;
    pubAdapters = adapters;
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

    EnterCriticalSection(&publishLock);

    if (cpu)
        *cpu = pubCpu;
    if (ram)
        *ram = pubRam;
    if (gpu)
        *gpu = pubGpu;
    if (gpuBusyMs)
        *gpuBusyMs = pubGpuBusyMs;
    if (gpuBusyValid)
        *gpuBusyValid = pubGpuBusyValid;
    if (adaptersOut)
        *adaptersOut = pubAdapters;

    LeaveCriticalSection(&publishLock);
}

void resource_usage_bi::setSamplerInterval(int intervalMs)
{
    if (intervalMs < 50)
        intervalMs = 50;
    if (intervalMs > 5000)
        intervalMs = 5000;

    InterlockedExchange(&samplerIntervalMs, (LONG)intervalMs);
}

void resource_usage_bi::setSamplerTarget(DWORD pid)
{
    InterlockedExchange(&samplerTargetPid, (LONG)pid);
}

void resource_usage_bi::startSampler(int intervalMs)
{
    if (samplerThread)
        return;

    setSamplerInterval(intervalMs);

    samplerStop = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!samplerStop)
    {
        log_bi::write("sampler: could not create the stop event");
        return;
    }

    samplerThread = CreateThread(NULL, 0, &resource_usage_bi::samplerEntry, this, 0, NULL);

    if (!samplerThread)
    {
        log_bi::write("sampler: could not start the worker thread");
        CloseHandle(samplerStop);
        samplerStop = NULL;
        return;
    }

    SetThreadPriority(samplerThread, THREAD_PRIORITY_BELOW_NORMAL);

    log_bi::write("sampler: worker started at %d ms", intervalMs);
}

void resource_usage_bi::stopSampler()
{
    if (!samplerThread)
        return;

    if (samplerStop)
        SetEvent(samplerStop);

    WaitForSingleObject(samplerThread, 4000);

    CloseHandle(samplerThread);
    samplerThread = NULL;

    if (samplerStop)
    {
        CloseHandle(samplerStop);
        samplerStop = NULL;
    }
}

bool resource_usage_bi::updateGpuMemory()
{
    gpuInfo.vramAvailable = false;
    for (auto &a : adapters)
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

    if (itemCount > adapters.size())
        adapters.resize(itemCount);

    for (DWORD i = 0; i < itemCount; ++i)
    {
        if (items[i].FmtValue.CStatus != ERROR_SUCCESS)
            continue;

        LONGLONG val = items[i].FmtValue.largeValue;

        if (val > 0)
        {
            total += val;
            adapters[i].usedVramMB = (double)val / (1024.0 * 1024.0);
            adapters[i].available = true;

            if (adapters[i].adapterName.empty())
            {
                char buf[256];
                int len = WideCharToMultiByte(CP_UTF8, 0, items[i].szName, -1,
                                              buf, sizeof(buf), NULL, NULL);
                if (len > 0)
                    adapters[i].adapterName = buf;
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

    gpuInfo.vramUsedMB = (double)total / (1024.0 * 1024.0);
    gpuInfo.vramAvailable = true;

    return true;
}

bool resource_usage_bi::updateCpuPower()
{
    cpuInfo.packagePowerAvailable = false;
    gpuInfo.gpuPowerAvailable = false;

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
        gpuInfo.gpuPowerW = gpuMilliwatts / 1000.0;
        gpuInfo.gpuPowerAvailable = true;
        gpuInfo.gpuPower = std::format("{:.2f} W", gpuInfo.gpuPowerW);
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

    cpuInfo.packagePowerW = packageMilliwatts / 1000.0;
    cpuInfo.packagePowerAvailable = true;
    cpuInfo.packagePower = std::format("{:.2f} W", cpuInfo.packagePowerW);

    return true;
}

bool resource_usage_bi::updateTemps()
{
    cpuInfo.cpuTempAvailable = false;
    gpuInfo.gpuTempAvailable = false;

    HRESULT hres = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (FAILED(hres) && hres != RPC_E_CHANGED_MODE)
        return false;

    IWbemLocator *pLoc = nullptr;
    hres = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
                            IID_IWbemLocator, (LPVOID *)&pLoc);
    if (FAILED(hres))
    {
        CoUninitialize();
        return false;
    }

    IWbemServices *pSvc = nullptr;
    BSTR ns = SysAllocString(L"ROOT\\WMI");
    hres = pLoc->ConnectServer(ns, NULL, NULL, NULL, 0, NULL, NULL, &pSvc);
    SysFreeString(ns);

    if (FAILED(hres))
    {
        pLoc->Release();
        CoUninitialize();
        return false;
    }

    hres = CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
                             RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                             NULL, EOAC_NONE);
    if (FAILED(hres))
    {
        pSvc->Release();
        pLoc->Release();
        CoUninitialize();
        return false;
    }

    BSTR lang = SysAllocString(L"WQL");
    BSTR query = SysAllocString(L"SELECT * FROM MSAcpi_ThermalZoneTemperature");
    IEnumWbemClassObject *pEnum = nullptr;
    hres = pSvc->ExecQuery(lang, query,
                           WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                           NULL, &pEnum);
    SysFreeString(lang);
    SysFreeString(query);

    if (SUCCEEDED(hres) && pEnum)
    {
        IWbemClassObject *pObj = nullptr;
        ULONG ret = 0;

        while (pEnum->Next(10000, 1, &pObj, &ret) == S_OK)
        {
            VARIANT vt;
            VariantInit(&vt);

            if (SUCCEEDED(pObj->Get(L"CurrentTemperature", 0, &vt, 0, 0)) &&
                vt.vt == VT_I4)
            {
                double tempC = ((int)vt.uintVal - 2732) / 10.0;

                VARIANT inst;
                VariantInit(&inst);
                std::string name = "CPU";
                if (SUCCEEDED(pObj->Get(L"InstanceName", 0, &inst, 0, 0)) &&
                    inst.vt == VT_BSTR)
                {
                    char buf[256];
                    WideCharToMultiByte(CP_UTF8, 0, inst.bstrVal, -1, buf, sizeof(buf), NULL, NULL);
                    name = buf;
                }
                VariantClear(&inst);

                if (name.find("CPU") != std::string::npos || name.find("cpu") != std::string::npos ||
                    name.find("CPUZ") != std::string::npos)
                {
                    cpuInfo.cpuTempC = tempC;
                    cpuInfo.cpuTempAvailable = true;
                }
                else
                {
                    gpuInfo.gpuTempC = tempC;
                    gpuInfo.gpuTempAvailable = true;
                }
            }
            VariantClear(&vt);
            pObj->Release();
        }
        pEnum->Release();
    }

    pSvc->Release();
    pLoc->Release();
    CoUninitialize();

    return cpuInfo.cpuTempAvailable || gpuInfo.gpuTempAvailable;
}

bool resource_usage_bi::updateGpuTime(DWORD pid, double *busyMsOut)
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

        gpuInfo.gpuLoadValue = load;
        gpuInfo.gpuLoad = std::format("{:.2f}%", load);
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
    CoUninitialize();

    if (sharedQuery != NULL)
    {
        PdhCloseQuery(sharedQuery);
        sharedQuery = NULL;
    }

    cpuTotalCounter = NULL;
    cpuCoreCounters.clear();
    gpuRunningCounter = NULL;
    powerCounter = NULL;
    sharedCollected = false;

    gpuTimePrev.clear();
    gpuBuffer.clear();
    powerBuffer.clear();

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
