#ifndef RESOURCE_USAGE_BI_H
#define RESOURCE_USAGE_BI_H

#include <string>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <map>
#include <cstdint>
#include <pdh.h>
#include <pdhmsg.h>
#include <iphlpapi.h>
#include <comdef.h>
#include <Wbemidl.h>

#include "autostart_bi.h"

#define DIV 1048576

class resource_usage_bi
{
public:
    resource_usage_bi()
    {
        CoInitializeEx(NULL, COINIT_MULTITHREADED);
        initCpuInfo();
        initGpuInfo();

        start_As_Admin = isStartAsAdminEnabled();
        start_With_Windows = isStartWithWindowsEnabled();
    }

    bool isStartWithWindowsEnabled();
    bool enableStartWithWindows();
    bool disableStartWithWindows();
    bool toggleStartWithWindows();

    bool isStartAsAdminEnabled();
    bool toggleStartAsAdmin();

    struct CpuInfo
    {
        std::string UsagePercent;
        std::vector<std::string> CoreUsagePercents;

        std::string cpuName;
        std::string architecture;

        double UsageValue = 0.0;

        std::string packagePower;
        double packagePowerW = 0.0;
        bool packagePowerAvailable = false;

        bool show_cpuName = false;
        bool show_architecture = false;
        bool show_UsagePercent = true;
        bool show_CoreUsagePercents = false;
        bool show_packagePower = true;
    };

    struct RamInfo
    {
        std::string dwMemoryLoad;
        std::string ullTotalPhys;
        std::string ullAvailPhys;
        std::string ullTotalPageFile;
        std::string ullAvailPageFile;
        std::string ullTotalVirtual;
        std::string ullAvailVirtual;
        std::string ullAvailExtendedVirtual;

        double loadValue = 0.0;
        double commitValue = 0.0;
        double usedGB = 0.0;
        double totalGB = 0.0;

        bool show_dwMemoryLoad = true;
        bool show_ullTotalPhys = true;
        bool show_ullAvailPhys = false;
        bool show_ullTotalPageFile = false;
        bool show_ullAvailPageFile = false;
        bool show_ullTotalVirtual = false;
        bool show_ullAvailVirtual = false;
        bool show_ullAvailExtendedVirtual = false;

    };

    struct DiskInfo
    {
        std::string diskLetter;
        std::string totalSpace;
        std::string freeSpace;
        std::string usedSpace;
        std::string usagePercent;

        bool show_diskLetter = false;
        bool show_totalSpace = false;
        bool show_freeSpace = false;
        bool show_usedSpace = false;
        bool show_usagePercent = false;
    };

    struct NetworkInfo
    {
        std::string interfaceName;
        std::string downloadSpeed;
        std::string uploadSpeed;

        bool show_interfaceName = false;
        bool show_downloadSpeed = false;
        bool show_uploadSpeed = false;
    };

    struct GpuInfo
    {
        std::string gpuName;
        std::string gpuLoad;

        double gpuLoadValue = 0.0;

        bool show_gpuName = true;
        bool show_gpuLoad = true;
    };

    CpuInfo cpuInfo;
    RamInfo ramInfo;
    GpuInfo gpuInfo;
    std::vector<DiskInfo> disksInfo;
    std::vector<NetworkInfo> networkInfo;

    bool start_With_Windows = true;
    bool start_As_Admin = false;
    bool minimize_To_Tray = false;
    bool exit_on_key_esc = false;

    bool updateRam();
    bool updateCpu();
    void initCpuInfo();
    bool updateGpu();
    void initGpuInfo();

    bool updateGpuTime(DWORD pid, double *busyMsOut);
    bool updateCpuPower();
    bool updateDisk();
    bool updateNetwork();

    bool updateAll()
    {
        bool success = true;
        success &= updateRam();
        success &= collectShared();
        success &= updateCpu();
        success &= updateGpu();
        updateCpuPower();
        success &= updateDisk();
        success &= updateNetwork();
        return success;
    }

    bool updateHudSample()
    {
        if (!collectShared())
            return false;

        bool success = true;
        success &= updateCpu();
        updateCpuPower();
        return success;
    }

    void cleanup();

private:
    bool openSharedQuery();
    bool collectShared();

    PDH_HQUERY sharedQuery = NULL;
    bool sharedQueryFailed = false;
    bool sharedCollected = false;

    PDH_HCOUNTER cpuTotalCounter = NULL;
    std::vector<PDH_HCOUNTER> cpuCoreCounters;
    DWORD cpuCoreCount = 0;

    PDH_HCOUNTER gpuRunningCounter = NULL;
    ULONGLONG gpuLastTick = 0;
    DWORD gpuTimePid = 0;
    int gpuTimeMisses = 0;

    PDH_HCOUNTER powerCounter = NULL;
    bool powerLogged = false;
    int powerAttempts = 0;

    std::vector<BYTE> gpuBuffer;
    std::vector<BYTE> powerBuffer;

    std::map<std::wstring, ULONGLONG> gpuTimePrev;

    struct NetCounters
    {
        DWORD in;
        DWORD out;
    };

    std::map<DWORD, NetCounters> netPrev;
    ULONGLONG netPrevTick = 0;
};

#endif
