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
#include <dxgi.h>
#include <dxgi1_2.h>

#include "autostart_bi.h"
#include "interfaces_bi.h"

#define DIV 1048576

class resource_usage_bi : public IResourceUsage
{
public:
    resource_usage_bi()
    {
        CoInitializeEx(NULL, COINIT_MULTITHREADED);
        initCpuInfo();
        initGpuInfo();

        start_As_Admin = isStartAsAdminEnabled();
        start_With_Windows = isStartWithWindowsEnabled();

        InitializeCriticalSection(&publishLock);
        publishLockReady = true;
    }

    ~resource_usage_bi()
    {
        stopSampler();

        if (publishLockReady)
        {
            DeleteCriticalSection(&publishLock);
            publishLockReady = false;
        }
    }

    bool isStartWithWindowsEnabled() override;
    bool enableStartWithWindows() override;
    bool disableStartWithWindows() override;
    bool toggleStartWithWindows() override;

    bool isStartAsAdminEnabled() override;
    bool toggleStartAsAdmin() override;

    using CpuInfo = IResourceUsage::CpuInfo;
    using RamInfo = IResourceUsage::RamInfo;
    using DiskInfo = IResourceUsage::DiskInfo;
    using NetworkInfo = IResourceUsage::NetworkInfo;
    using GpuInfo = IResourceUsage::GpuInfo;
    using AdapterInfo = IResourceUsage::AdapterInfo;
    using mem_unit_bi = IResourceUsage::mem_unit_bi;

    CpuInfo cpuInfo;
    RamInfo ramInfo;
    GpuInfo gpuInfo;
    std::vector<AdapterInfo> adapters;
    std::vector<DiskInfo> disksInfo;
    std::vector<NetworkInfo> networkInfo;

    int memUnit = MEM_UNIT_AUTO;

    bool start_With_Windows = true;
    bool start_As_Admin = false;
    bool minimize_To_Tray = false;
    bool exit_on_key_esc = false;

    bool updateRam() override;
    bool updateCpu() override;
    void initCpuInfo();
    bool updateGpu() override;
    void initGpuInfo();

    bool updateGpuTime(DWORD pid, double *busyMsOut) override;
    bool updateGpuMemory() override;
    bool updateCpuPower() override;
    bool updateTemps() override;
    bool updateDisk() override;
    bool updateNetwork() override;

    bool updateAll() override
    {
        bool success = true;
        success &= updateRam();
        success &= collectShared();
        success &= updateCpu();
        success &= updateGpu();
        updateCpuPower();
        updateTemps();
        success &= updateDisk();
        success &= updateNetwork();
        return success;
    }

    bool updateHudSample() override
    {
        if (!collectShared())
            return false;

        bool success = true;
        success &= updateCpu();
        updateCpuPower();
        updateTemps();
        updateGpuMemory();
        return success;
    }

    void cleanup() override;

    void startSampler(int intervalMs) override;
    void stopSampler() override;
    void setSamplerInterval(int intervalMs) override;
    void setSamplerTarget(DWORD pid) override;
    int samplerInterval() const override { return (int)samplerIntervalMs; }

    void readSnapshot(CpuInfo *cpu, RamInfo *ram, GpuInfo *gpu,
                      double *gpuBusyMs, bool *gpuBusyValid,
                      std::vector<AdapterInfo> *adaptersOut = nullptr) override;

private:
    static DWORD WINAPI samplerEntry(LPVOID param);
    void samplerLoop();
    void publishSample(double gpuBusyMs, bool gpuBusyValid);

    CRITICAL_SECTION publishLock;
    bool publishLockReady = false;

    CpuInfo pubCpu;
    RamInfo pubRam;
    GpuInfo pubGpu;
    double pubGpuBusyMs = 0.0;
    bool pubGpuBusyValid = false;
    std::vector<AdapterInfo> pubAdapters;

    HANDLE samplerThread = NULL;
    HANDLE samplerStop = NULL;
    volatile LONG samplerIntervalMs = 200;
    volatile LONG samplerTargetPid = 0;

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

    PDH_HCOUNTER vramCounter = NULL;
    bool vramLogged = false;

    std::vector<BYTE> gpuBuffer;
    std::vector<BYTE> powerBuffer;
    std::vector<BYTE> vramBuffer;

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
