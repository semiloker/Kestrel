#ifndef INTERFACES_BI_H
#define INTERFACES_BI_H

#include <string>
#include <vector>
#include <variant>
#include <windows.h>

// ─── Result<T,E> ───────────────────────────────────────────────────────────

template<typename T, typename E = std::string>
class Result
{
public:
    Result(const T &val) : store(val) {}
    Result(T &&val) : store(std::move(val)) {}
    Result(const E &err) : store(err) {}
    Result(E &&err) : store(std::move(err)) {}

    bool ok() const { return std::holds_alternative<T>(store); }
    T &value() { return std::get<T>(store); }
    const T &value() const { return std::get<T>(store); }
    E &error() { return std::get<E>(store); }
    const E &error() const { return std::get<E>(store); }

    T value_or(T &&fallback) const
    {
        if (ok()) return value();
        return std::move(fallback);
    }

private:
    std::variant<T, E> store;
};

template<typename E>
class Result<void, E>
{
public:
    Result() : ok_(true) {}
    Result(const E &err) : ok_(false), err_(err) {}
    Result(E &&err) : ok_(false), err_(std::move(err)) {}

    bool ok() const { return ok_; }
    E &error() { return err_; }
    const E &error() const { return err_; }

private:
    bool ok_;
    E err_;
};

// ─── IResourceUsage ───────────────────────────────────────────────────────

class IResourceUsage
{
public:
    virtual ~IResourceUsage() = default;

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
        double cpuTempC = 0.0;
        bool cpuTempAvailable = false;
        bool show_cpuTemp = false;
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
        double vramUsedMB = 0.0;
        double vramTotalMB = 0.0;
        bool vramAvailable = false;
        std::string gpuPower;
        double gpuPowerW = 0.0;
        bool gpuPowerAvailable = false;
        double gpuTempC = 0.0;
        bool gpuTempAvailable = false;
        bool show_gpuTemp = false;
        bool show_gpuName = true;
        bool show_gpuLoad = true;
        bool show_vram = false;
        bool show_gpuPower = false;
        bool show_adapters = false;
    };

    struct AdapterInfo
    {
        std::string adapterName;
        double usedVramMB = 0.0;
        double totalVramMB = 0.0;
        bool available = false;
    };

    enum mem_unit_bi { MEM_UNIT_AUTO = 0, MEM_UNIT_MB, MEM_UNIT_GB };

    virtual bool updateRam() = 0;
    virtual bool updateCpu() = 0;
    virtual bool updateGpu() = 0;
    virtual bool updateGpuTime(DWORD pid, double *busyMsOut) = 0;
    virtual bool updateGpuMemory() = 0;
    virtual bool updateCpuPower() = 0;
    virtual bool updateTemps() = 0;
    virtual bool updateDisk() = 0;
    virtual bool updateNetwork() = 0;
    virtual bool updateAll() = 0;
    virtual bool updateHudSample() = 0;

    virtual void cleanup() = 0;
    virtual void startSampler(int intervalMs) = 0;
    virtual void stopSampler() = 0;
    virtual void setSamplerInterval(int intervalMs) = 0;
    virtual void setSamplerTarget(DWORD pid) = 0;
    virtual int samplerInterval() const = 0;

    virtual void readSnapshot(CpuInfo *cpu, RamInfo *ram, GpuInfo *gpu,
                              double *gpuBusyMs, bool *gpuBusyValid,
                              std::vector<AdapterInfo> *adaptersOut = nullptr) = 0;

    virtual bool isStartWithWindowsEnabled() = 0;
    virtual bool enableStartWithWindows() = 0;
    virtual bool disableStartWithWindows() = 0;
    virtual bool toggleStartWithWindows() = 0;
    virtual bool isStartAsAdminEnabled() = 0;
    virtual bool toggleStartAsAdmin() = 0;
};

// ─── IBatteryInfo ──────────────────────────────────────────────────────────

class IBatteryInfo
{
public:
    virtual ~IBatteryInfo() = default;

    struct bi_struct_static
    {
        std::string Chemistry;
        std::string DesignedCapacity;
        std::string FullChargedCapacity;
        std::string DefaultAlert1;
        std::string DefaultAlert2;
        std::string WearLevel;
        std::string CycleCount;
        double designedWh = 0.0;
        double fullChargedWh = 0.0;
        double alert1Wh = 0.0;
        double alert2Wh = 0.0;
        double wearPercent = 0.0;
        int cycleCount = 0;
        bool capacityValid = false;
        bool wearValid = false;
        bool cycleCountValid = false;
        bool alertsValid = false;
    };

    struct bi_struct_dynamic_1s
    {
        std::string Voltage;
        std::string Rate;
        std::string PowerState;
        std::string RemainingCapacity;
        std::string ChargeLevel;
        double voltageV = 0.0;
        double rateW = 0.0;
        double remainingWh = 0.0;
        double chargePercent = 0.0;
        bool voltageValid = false;
        bool rateValid = false;
        bool remainingValid = false;
        bool chargeValid = false;
        bool charging = false;
        bool discharging = false;
        bool onLine = false;
        bool Voltage_ = false;
        bool Rate_ = false;
        bool PowerState_ = false;
        bool RemainingCapacity_ = false;
        bool ChargeLevel_ = false;
    };

    struct bi_struct_dynamic_10s
    {
        std::string TimeRemaining;
        std::string TimeToFullCharge;
        int minutesToEmpty = -1;
        int minutesToFull = -1;
        bool TimeRemaining_ = false;
    };

    virtual bool Initialize() = 0;
    virtual bool QueryTag() = 0;
    virtual bool QueryBatteryInfo() = 0;
    virtual bool QueryBatteryStatus() = 0;
    virtual bool QueryBatteryRemaining() = 0;
    virtual bool QueryBatteryCycleCount() = 0;
};

// ─── IETWTrace ─────────────────────────────────────────────────────────────

class IETWTrace
{
public:
    virtual ~IETWTrace() = default;

    enum api_bi { API_NONE = 0, API_D3D9, API_D3D11, API_D3D12, API_OPENGL, API_VULKAN };
    enum provider_bi { PROV_DXGI = 0, PROV_D3D9, PROV_DXGKRNL, PROV_COUNT };

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

    struct frame_sample_bi
    {
        LONGLONG time100ns;
        float intervalMs;
    };

    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool running() const = 0;
    virtual bool elevated() const = 0;
    virtual bool hasFailed() const = 0;
    virtual const char *lastError() const = 0;

    virtual void setTarget(DWORD processId) = 0;
    virtual DWORD target() const = 0;

    virtual sample_bi sample() = 0;
    virtual size_t drainFrames(frame_sample_bi *out, size_t max) = 0;
    virtual unsigned long long framesDropped() const = 0;

    virtual void setFallbackSource(provider_bi provider, unsigned eventId) = 0;
    virtual void setDeepCensus(bool enabled) = 0;
    virtual void autoConfigForApi(api_bi api) = 0;

    static const char *apiName(api_bi a);
    static const char *providerName(provider_bi p);
    static api_bi detectApi(DWORD processId);
    static std::string processName(DWORD processId);
};

// ─── IOverlay ───────────────────────────────────────────────────────────────

class IOverlay
{
public:
    virtual ~IOverlay() = default;

    enum corner_bi { CORNER_TOP_LEFT = 0, CORNER_TOP_RIGHT, CORNER_BOTTOM_LEFT, CORNER_BOTTOM_RIGHT };

    virtual void updateClickThrough() = 0;

    virtual void setScale(int percent) = 0;
    virtual int getScale() const = 0;

    virtual void CreateOverlayWindow(HINSTANCE hInstance, HWND parentHwnd = NULL) = 0;
    virtual void DestroyOverlayWindow() = 0;
    virtual void Render() = 0;
    virtual void UpdatePosition() = 0;
    virtual void ForceTopMost() = 0;
};

#endif
