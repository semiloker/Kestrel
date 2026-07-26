#ifndef GPU_SENSOR_BI_H
#define GPU_SENSOR_BI_H

#include <windows.h>
#include <string>
#include <vector>

// Reads the GPU thermal sensor through interfaces an unprivileged process is
// allowed to touch.
//
// Primary source is the WDDM adapter perf data (D3DKMTQueryAdapterInfo /
// KMTQAITYPE_ADAPTERPERFDATA) - the same block Task Manager reads, vendor
// neutral and driver supplied. NVML is a fallback for NVIDIA boards whose
// driver leaves the WDDM fields at zero.
//
// ACPI thermal zones are deliberately not used here: they describe chassis and
// board zones and say nothing about the graphics die.
class gpu_sensor_bi
{
public:
    gpu_sensor_bi() = default;
    ~gpu_sensor_bi();

    gpu_sensor_bi(const gpu_sensor_bi &) = delete;
    gpu_sensor_bi &operator=(const gpu_sensor_bi &) = delete;

    struct reading_bi
    {
        double temperatureC = 0.0;
        double temperatureMaxC = 0.0;  // driver's shutdown limit, 0 when unknown
        std::string adapterName;
        const char *source = "-";
    };

    // Returns false when no adapter reports a usable temperature.
    bool readTemperature(reading_bi *out);

private:
    struct adapter_bi
    {
        LUID luid;
        std::string name;
        unsigned long long dedicatedBytes;
        unsigned vendorId;
    };

    bool ensureD3dkmt();
    bool ensureAdapters();
    bool readD3dkmt(reading_bi *out);

    bool ensureNvml();
    bool readNvml(reading_bi *out);

    HMODULE gdi_ = NULL;
    void *openAdapter_ = NULL;
    void *queryAdapter_ = NULL;
    void *closeAdapter_ = NULL;
    bool d3dkmtChecked_ = false;

    std::vector<adapter_bi> adapters_;
    bool adaptersChecked_ = false;

    HMODULE nvml_ = NULL;
    bool nvmlChecked_ = false;
    bool nvmlStarted_ = false;
    void *nvmlShutdown_ = NULL;
    void *nvmlGetCount_ = NULL;
    void *nvmlGetHandle_ = NULL;
    void *nvmlGetTemp_ = NULL;
    void *nvmlGetName_ = NULL;
};

#endif
