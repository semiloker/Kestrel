#include "gpu_sensor_bi.h"
#include "logger_bi.h"

#include <dxgi.h>
#include <dxgi1_2.h>
#include <shlobj.h>

#include <algorithm>
#include <vector>

// ─── D3DKMT ────────────────────────────────────────────────────────────────
//
// These structures are part of the WDDM kernel interface. They are declared
// here rather than pulled from d3dkmthk.h because the MinGW headers do not
// carry the perf-data types, and the entry points are resolved dynamically so
// the app still starts on a system whose gdi32 lacks them.

namespace
{

typedef UINT32 kmt_handle_bi;

struct kmt_open_from_luid_bi
{
    LUID AdapterLuid;
    kmt_handle_bi hAdapter;
};

struct kmt_close_adapter_bi
{
    kmt_handle_bi hAdapter;
};

struct kmt_query_adapter_info_bi
{
    kmt_handle_bi hAdapter;
    UINT Type;
    void *pPrivateDriverData;
    UINT PrivateDriverDataSize;
};

struct kmt_adapter_perfdata_bi
{
    ULONG PhysicalAdapterIndex;
    ULONGLONG MemoryFrequency;
    ULONGLONG MaxMemoryFrequency;
    ULONGLONG MaxMemoryFrequencyOC;
    ULONGLONG MemoryBandwidth;
    ULONGLONG PCIEBandwidth;
    ULONG FanRPM;
    ULONG Power;
    ULONG Temperature;  // deci-degrees Celsius
    UCHAR PowerStateOverride;
};

struct kmt_adapter_perfdatacaps_bi
{
    ULONG PhysicalAdapterIndex;
    ULONGLONG MaxMemoryBandwidth;
    ULONGLONG MaxPCIEBandwidth;
    ULONG MaxFanRPM;
    ULONG TemperatureMax;      // deci-degrees Celsius
    ULONG TemperatureWarning;  // deci-degrees Celsius
};

const UINT KMTQAITYPE_ADAPTERPERFDATA_BI = 62;
const UINT KMTQAITYPE_ADAPTERPERFDATACAPS_BI = 63;

typedef NTSTATUS(WINAPI *pfn_open_from_luid_bi)(kmt_open_from_luid_bi *);
typedef NTSTATUS(WINAPI *pfn_query_adapter_info_bi)(kmt_query_adapter_info_bi *);
typedef NTSTATUS(WINAPI *pfn_close_adapter_bi)(kmt_close_adapter_bi *);

// ─── NVML ──────────────────────────────────────────────────────────────────

typedef int (*pfn_nvml_init_bi)(void);
typedef int (*pfn_nvml_shutdown_bi)(void);
typedef int (*pfn_nvml_count_bi)(unsigned int *);
typedef int (*pfn_nvml_handle_bi)(unsigned int, void **);
typedef int (*pfn_nvml_temp_bi)(void *, int, unsigned int *);
typedef int (*pfn_nvml_name_bi)(void *, char *, unsigned int);

const int NVML_TEMPERATURE_GPU_BI = 0;

#ifndef LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
#define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR 0x00000100
#endif

#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
#endif

// A reading outside this band means the driver handed us a placeholder rather
// than a sensor value.
bool plausibleTemp(double c)
{
    return c > 0.5 && c < 150.0;
}

std::string narrow(const wchar_t *w)
{
    if (!w)
        return std::string();

    char buf[256];
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, buf, sizeof(buf), NULL, NULL);
    if (len <= 0)
        return std::string();

    return std::string(buf);
}

HMODULE loadTrustedNvml()
{
    std::vector<wchar_t> systemDirectory(MAX_PATH);
    UINT systemLength = GetSystemDirectoryW(&systemDirectory[0],
                                            (UINT)systemDirectory.size());
    if (systemLength > 0 && systemLength < systemDirectory.size())
    {
        std::wstring path(&systemDirectory[0], systemLength);
        path += L"\\nvml.dll";

        HMODULE module = LoadLibraryExW(
            path.c_str(), NULL,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (module)
            return module;
    }

    wchar_t programFiles[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROGRAM_FILES, NULL,
                                   SHGFP_TYPE_CURRENT, programFiles)))
    {
        std::wstring path(programFiles);
        path += L"\\NVIDIA Corporation\\NVSMI\\nvml.dll";

        HMODULE module = LoadLibraryExW(
            path.c_str(), NULL,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (module)
            return module;
    }

    return NULL;
}

}  // namespace

gpu_sensor_bi::~gpu_sensor_bi()
{
    if (nvmlStarted_ && nvmlShutdown_)
        ((pfn_nvml_shutdown_bi)nvmlShutdown_)();

    if (nvml_)
        FreeLibrary(nvml_);

    if (gdi_)
        FreeLibrary(gdi_);
}

bool gpu_sensor_bi::ensureD3dkmt()
{
    if (d3dkmtChecked_)
        return queryAdapter_ != NULL;

    d3dkmtChecked_ = true;

    gdi_ = LoadLibraryA("gdi32.dll");
    if (!gdi_)
        return false;

    openAdapter_ = (void *)GetProcAddress(gdi_, "D3DKMTOpenAdapterFromLuid");
    queryAdapter_ = (void *)GetProcAddress(gdi_, "D3DKMTQueryAdapterInfo");
    closeAdapter_ = (void *)GetProcAddress(gdi_, "D3DKMTCloseAdapter");

    if (!openAdapter_ || !queryAdapter_)
    {
        log_bi::write("gpu temp: gdi32 has no D3DKMT adapter entry points");
        queryAdapter_ = NULL;
        return false;
    }

    return true;
}

bool gpu_sensor_bi::ensureAdapters()
{
    if (adaptersChecked_)
        return !adapters_.empty();

    adaptersChecked_ = true;

    IDXGIFactory1 *factory = NULL;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&factory)) || !factory)
    {
        log_bi::write("gpu temp: CreateDXGIFactory1 failed, no adapters to poll");
        return false;
    }

    for (UINT i = 0;; ++i)
    {
        IDXGIAdapter1 *adapter = NULL;
        if (FAILED(factory->EnumAdapters1(i, &adapter)) || !adapter)
            break;

        DXGI_ADAPTER_DESC1 desc;
        if (SUCCEEDED(adapter->GetDesc1(&desc)) &&
            (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0)
        {
            adapter_bi a;
            a.luid = desc.AdapterLuid;
            a.name = narrow(desc.Description);
            a.dedicatedBytes = (unsigned long long)desc.DedicatedVideoMemory;
            a.vendorId = desc.VendorId;
            adapters_.push_back(a);
        }

        adapter->Release();
    }

    factory->Release();

    // Discrete boards carry the sensor worth showing, and they are the ones
    // with dedicated video memory. Poll them first.
    std::sort(adapters_.begin(), adapters_.end(),
              [](const adapter_bi &l, const adapter_bi &r)
              { return l.dedicatedBytes > r.dedicatedBytes; });

    return !adapters_.empty();
}

bool gpu_sensor_bi::readD3dkmt(reading_bi *out)
{
    if (!ensureD3dkmt() || !ensureAdapters())
        return false;

    pfn_open_from_luid_bi openFn = (pfn_open_from_luid_bi)openAdapter_;
    pfn_query_adapter_info_bi queryFn = (pfn_query_adapter_info_bi)queryAdapter_;
    pfn_close_adapter_bi closeFn = (pfn_close_adapter_bi)closeAdapter_;

    for (size_t i = 0; i < adapters_.size(); ++i)
    {
        kmt_open_from_luid_bi open;
        ZeroMemory(&open, sizeof(open));
        open.AdapterLuid = adapters_[i].luid;

        if (openFn(&open) != 0 || open.hAdapter == 0)
            continue;

        kmt_adapter_perfdata_bi perf;
        ZeroMemory(&perf, sizeof(perf));
        perf.PhysicalAdapterIndex = 0;

        kmt_query_adapter_info_bi qai;
        ZeroMemory(&qai, sizeof(qai));
        qai.hAdapter = open.hAdapter;
        qai.Type = KMTQAITYPE_ADAPTERPERFDATA_BI;
        qai.pPrivateDriverData = &perf;
        qai.PrivateDriverDataSize = sizeof(perf);

        bool got = false;

        if (queryFn(&qai) == 0)
        {
            double celsius = (double)perf.Temperature / 10.0;

            if (plausibleTemp(celsius))
            {
                out->temperatureC = celsius;
                out->adapterName = adapters_[i].name;
                out->source = "d3dkmt";
                out->temperatureMaxC = 0.0;

                kmt_adapter_perfdatacaps_bi caps;
                ZeroMemory(&caps, sizeof(caps));
                caps.PhysicalAdapterIndex = 0;

                kmt_query_adapter_info_bi qcaps;
                ZeroMemory(&qcaps, sizeof(qcaps));
                qcaps.hAdapter = open.hAdapter;
                qcaps.Type = KMTQAITYPE_ADAPTERPERFDATACAPS_BI;
                qcaps.pPrivateDriverData = &caps;
                qcaps.PrivateDriverDataSize = sizeof(caps);

                if (queryFn(&qcaps) == 0 && caps.TemperatureMax > 0)
                    out->temperatureMaxC = (double)caps.TemperatureMax / 10.0;

                got = true;
            }
        }

        if (closeFn)
        {
            kmt_close_adapter_bi close;
            close.hAdapter = open.hAdapter;
            closeFn(&close);
        }

        if (got)
            return true;
    }

    return false;
}

bool gpu_sensor_bi::ensureNvml()
{
    if (nvmlChecked_)
        return nvmlStarted_;

    nvmlChecked_ = true;

    // Only worth loading when an NVIDIA adapter is actually present.
    if (!ensureAdapters())
        return false;

    bool haveNvidia = false;
    for (size_t i = 0; i < adapters_.size(); ++i)
    {
        if (adapters_[i].vendorId == 0x10DE)
        {
            haveNvidia = true;
            break;
        }
    }

    if (!haveNvidia)
        return false;

    nvml_ = loadTrustedNvml();

    if (!nvml_)
        return false;

    pfn_nvml_init_bi init = (pfn_nvml_init_bi)GetProcAddress(nvml_, "nvmlInit_v2");
    if (!init)
        init = (pfn_nvml_init_bi)GetProcAddress(nvml_, "nvmlInit");

    nvmlShutdown_ = (void *)GetProcAddress(nvml_, "nvmlShutdown");
    nvmlGetCount_ = (void *)GetProcAddress(nvml_, "nvmlDeviceGetCount_v2");
    if (!nvmlGetCount_)
        nvmlGetCount_ = (void *)GetProcAddress(nvml_, "nvmlDeviceGetCount");
    nvmlGetHandle_ = (void *)GetProcAddress(nvml_, "nvmlDeviceGetHandleByIndex_v2");
    if (!nvmlGetHandle_)
        nvmlGetHandle_ = (void *)GetProcAddress(nvml_, "nvmlDeviceGetHandleByIndex");
    nvmlGetTemp_ = (void *)GetProcAddress(nvml_, "nvmlDeviceGetTemperature");
    nvmlGetName_ = (void *)GetProcAddress(nvml_, "nvmlDeviceGetName");

    if (!init || !nvmlGetCount_ || !nvmlGetHandle_ || !nvmlGetTemp_)
    {
        FreeLibrary(nvml_);
        nvml_ = NULL;
        return false;
    }

    if (init() != 0)
    {
        FreeLibrary(nvml_);
        nvml_ = NULL;
        return false;
    }

    nvmlStarted_ = true;
    return true;
}

bool gpu_sensor_bi::readNvml(reading_bi *out)
{
    if (!ensureNvml())
        return false;

    unsigned int count = 0;
    if (((pfn_nvml_count_bi)nvmlGetCount_)(&count) != 0 || count == 0)
        return false;

    for (unsigned int i = 0; i < count; ++i)
    {
        void *dev = NULL;
        if (((pfn_nvml_handle_bi)nvmlGetHandle_)(i, &dev) != 0 || !dev)
            continue;

        unsigned int celsius = 0;
        if (((pfn_nvml_temp_bi)nvmlGetTemp_)(dev, NVML_TEMPERATURE_GPU_BI, &celsius) != 0)
            continue;

        if (!plausibleTemp((double)celsius))
            continue;

        out->temperatureC = (double)celsius;
        out->temperatureMaxC = 0.0;
        out->source = "nvml";
        out->adapterName.clear();

        if (nvmlGetName_)
        {
            char name[128] = {0};
            if (((pfn_nvml_name_bi)nvmlGetName_)(dev, name, sizeof(name)) == 0)
                out->adapterName = name;
        }

        return true;
    }

    return false;
}

bool gpu_sensor_bi::readTemperature(reading_bi *out)
{
    if (!out)
        return false;

    *out = reading_bi();

    if (readD3dkmt(out))
        return true;

    return readNvml(out);
}
