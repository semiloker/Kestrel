#ifndef RESOURCE_USAGE_BI_H
#define RESOURCE_USAGE_BI_H

#include <string>
#include <atomic>
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
#include "gpu_sensor_bi.h"
#include "interfaces_bi.h"
#include "mahm_sensor_bi.h"

#define DIV 1048576

class resource_usage_bi : public IResourceUsage
{
public:
    resource_usage_bi()
    {
        InitializeCriticalSection(&publishLock);
        publishLockReady = true;
        InitializeCriticalSection(&sampleLock);
        sampleLockReady = true;
        InitializeCriticalSection(&samplerLifecycleLock);
        samplerLifecycleLockReady = true;

        initCpuInfo();
        initGpuInfo();
        pubCpu = cpuInfo;
        pubRam = ramInfo;
        pubGpu = gpuInfo;
        pubAdapters = adapters;
        pubDisks = disksInfo;
        pubNetwork = networkInfo;
        samplerConfig = captureSamplerConfig();

        start_As_Admin = isStartAsAdminEnabled();
        start_With_Windows = isStartWithWindowsEnabled();
    }

    ~resource_usage_bi()
    {
        stopSampler();
        cleanup();

        if (publishLockReady)
        {
            DeleteCriticalSection(&publishLock);
            publishLockReady = false;
        }
        if (sampleLockReady)
        {
            DeleteCriticalSection(&sampleLock);
            sampleLockReady = false;
        }
        if (samplerLifecycleLockReady)
        {
            DeleteCriticalSection(&samplerLifecycleLock);
            samplerLifecycleLockReady = false;
        }
    }
    resource_usage_bi(const resource_usage_bi &) = delete;
    resource_usage_bi &operator=(const resource_usage_bi &) = delete;

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

    bool updateAll() override;
    bool updateHudSample() override;

    void cleanup() override;

    void startSampler(int intervalMs) override;
    void stopSampler() override;
    void setSamplerInterval(int intervalMs) override;
    void setSamplerTarget(DWORD pid) override;
    int samplerInterval() const override
    {
        return (int)samplerIntervalMs.load(std::memory_order_acquire);
    }

    void readSnapshot(CpuInfo *cpu, RamInfo *ram, GpuInfo *gpu,
                      double *gpuBusyMs, bool *gpuBusyValid,
                      std::vector<AdapterInfo> *adaptersOut = nullptr) override;

private:
    struct sampler_config_bi
    {
        int memUnit = MEM_UNIT_AUTO;
        bool showCpuCores = false;
        bool showRamTotalPhys = true;
        bool showRamAvailPhys = false;
        bool showRamTotalPageFile = false;
        bool showRamAvailPageFile = false;
        bool showRamTotalVirtual = false;
        bool showRamAvailVirtual = false;
        bool showRamAvailExtendedVirtual = false;
    };

    static DWORD WINAPI samplerEntry(LPVOID param);
    void samplerLoop();
    void publishSample(const CpuInfo &cpu, const RamInfo &ram, const GpuInfo &gpu,
                       const std::vector<AdapterInfo> &sampleAdapters,
                       const std::vector<DiskInfo> &sampleDisks,
                       const std::vector<NetworkInfo> &sampleNetwork,
                       double gpuBusyMs, bool gpuBusyValid);
    sampler_config_bi captureSamplerConfig() const;

    bool updateRamInto(RamInfo &ram, const sampler_config_bi &config);
    bool updateCpuInto(CpuInfo &cpu, bool showCores);
    bool updateGpuMemoryInto(GpuInfo &gpu, std::vector<AdapterInfo> &sampleAdapters);
    bool updateCpuPowerInto(CpuInfo &cpu, GpuInfo &gpu);
    bool updateGpuTimeInto(DWORD pid, double *busyMsOut, GpuInfo &gpu);
    bool updateTempsInto(CpuInfo &cpu, GpuInfo &gpu);
    bool updateDiskInto(std::vector<DiskInfo> &disks);
    bool updateNetworkInto(std::vector<NetworkInfo> &network);

    CRITICAL_SECTION publishLock;
    bool publishLockReady = false;
    CRITICAL_SECTION sampleLock;
    bool sampleLockReady = false;
    CRITICAL_SECTION samplerLifecycleLock;
    bool samplerLifecycleLockReady = false;

    CpuInfo pubCpu;
    RamInfo pubRam;
    GpuInfo pubGpu;
    double pubGpuBusyMs = 0.0;
    bool pubGpuBusyValid = false;
    std::vector<AdapterInfo> pubAdapters;
    std::vector<DiskInfo> pubDisks;
    std::vector<NetworkInfo> pubNetwork;
    sampler_config_bi samplerConfig;

    HANDLE samplerThread = NULL;
    HANDLE samplerStop = NULL;
    std::atomic<LONG> samplerIntervalMs{200};
    std::atomic<DWORD> samplerTargetPid{0};

    bool cleanupDone = false;

    bool openSharedQuery();
    bool collectShared();

    // Temperature sources are sampled by the worker. Public update wrappers
    // serialize access through sampleLock so the PDH queries stay single-owner.
    bool openTempQuery();
    bool readThermalZonePdh(double *celsiusOut);
    bool readThermalZoneWmi(double *celsiusOut);

    gpu_sensor_bi gpuSensor;
    mahm_sensor_bi mahmSensor;
    bool gpuTempLogged = false;
    bool cpuTempLogged = false;

    PDH_HQUERY tempQuery = NULL;
    PDH_HCOUNTER thermalPreciseCounter = NULL;
    PDH_HCOUNTER thermalCounter = NULL;
    bool tempQueryFailed = false;
    std::vector<BYTE> thermalBuffer;

    IWbemServices *thermalSvc = NULL;
    bool thermalWmiFailed = false;

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
