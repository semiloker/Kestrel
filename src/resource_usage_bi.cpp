#include "resource_usage_bi.h"
#include "logger_bi.h"

#include <cctype>
#include <cstring>
#include <cwchar>

static void formatMegabytes(std::string &out, ULONGLONG bytes)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%llu MB", (unsigned long long)(bytes / DIV));
    out = buf;
}

bool resource_usage_bi::updateRam()
{
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);

    if (!GlobalMemoryStatusEx(&statex))
        return false;

    ramInfo.loadValue = static_cast<double>(statex.dwMemoryLoad);

    if (statex.ullTotalPageFile > 0)
    {
        ramInfo.commitValue =
            (1.0 - (double)statex.ullAvailPageFile / (double)statex.ullTotalPageFile) * 100.0;
    }

    ramInfo.totalGB = static_cast<double>(statex.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0);
    ramInfo.usedGB = static_cast<double>(statex.ullTotalPhys - statex.ullAvailPhys) /
                     (1024.0 * 1024.0 * 1024.0);

    char buf[32];
    snprintf(buf, sizeof(buf), "%lu %%", (unsigned long)statex.dwMemoryLoad);
    ramInfo.dwMemoryLoad = buf;

    if (ramInfo.show_ullTotalPhys)
        formatMegabytes(ramInfo.ullTotalPhys, statex.ullTotalPhys);
    if (ramInfo.show_ullAvailPhys)
        formatMegabytes(ramInfo.ullAvailPhys, statex.ullAvailPhys);
    if (ramInfo.show_ullTotalPageFile)
        formatMegabytes(ramInfo.ullTotalPageFile, statex.ullTotalPageFile);
    if (ramInfo.show_ullAvailPageFile)
        formatMegabytes(ramInfo.ullAvailPageFile, statex.ullAvailPageFile);
    if (ramInfo.show_ullTotalVirtual)
        formatMegabytes(ramInfo.ullTotalVirtual, statex.ullTotalVirtual);
    if (ramInfo.show_ullAvailVirtual)
        formatMegabytes(ramInfo.ullAvailVirtual, statex.ullAvailVirtual);
    if (ramInfo.show_ullAvailExtendedVirtual)
        formatMegabytes(ramInfo.ullAvailExtendedVirtual, statex.ullAvailExtendedVirtual);

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

            char buf[64];
            snprintf(buf, sizeof(buf), "%.1f KB/s", down);
            netInfo.downloadSpeed = buf;
            snprintf(buf, sizeof(buf), "%.1f KB/s", up);
            netInfo.uploadSpeed = buf;
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

    if (PdhCollectQueryData(sharedQuery) != ERROR_SUCCESS)
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

    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f %%", cpuInfo.UsageValue);
    cpuInfo.UsagePercent = buf;

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
            snprintf(buf, sizeof(buf), "%.2f %%", core.doubleValue);
            cpuInfo.CoreUsagePercents[i] = buf;
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
}

bool resource_usage_bi::updateGpu()
{
    return updateGpuTime(0, NULL);
}

bool resource_usage_bi::updateCpuPower()
{
    cpuInfo.packagePowerAvailable = false;

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

        if (!strstr(name, "_pkg") && !strstr(name, "package"))
            continue;

        if (strstr(name, "core"))
            continue;

        if (items[i].FmtValue.doubleValue > packageMilliwatts)
            packageMilliwatts = items[i].FmtValue.doubleValue;
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

    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f W", cpuInfo.packagePowerW);
    cpuInfo.packagePower = buf;

    return true;
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

        char buf[32];
        snprintf(buf, sizeof(buf), "%.2f %%", load);
        gpuInfo.gpuLoad = buf;
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
