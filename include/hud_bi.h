#ifndef HUD_BI_H
#define HUD_BI_H

#include <windows.h>
#include <string>
#include <vector>
#include <cstddef>

#define HUD_SAMPLE_COUNT 240
#define HUD_SAMPLE_INTERVAL_MS 100
#define HUD_PDH_EVERY_N_TICKS 2

#define HUD_MIN_COLUMNS 27

#define HUD_MAX_COLUMNS 64

class hud_series_bi
{
public:
    explicit hud_series_bi(size_t capacity = HUD_SAMPLE_COUNT);

    void push(double value);
    void reset();

    double at(size_t i) const;

    size_t size() const { return count; }

    double current() const { return curValue; }
    double minimum() const { return minValue; }
    double maximum() const { return maxValue; }

    double percentile(double p) const;

private:
    std::vector<double> data;
    size_t head;
    size_t count;

    double curValue;
    double minValue;
    double maxValue;

    mutable std::vector<double> scratch;
};

enum hud_color_bi
{
    HUD_COLOR_WHITE = 0,
    HUD_COLOR_BLUE,
    HUD_COLOR_GREEN,
    HUD_COLOR_ORANGE,
    HUD_COLOR_CYAN,
    HUD_COLOR_MAGENTA,
    HUD_COLOR_RED
};

struct hud_row_bi
{
    std::string left;
    std::string right;
    hud_color_bi color;
};

enum hud_element_kind_bi
{
    HUD_EL_ROW = 0,
    HUD_EL_GRAPH
};

enum hud_metric_id_bi
{
    HUD_M_FPS = 0,
    HUD_M_PRE,
    HUD_M_GPUMS,
    HUD_M_CPU,
    HUD_M_GPU,
    HUD_M_RAM,
    HUD_M_COMMIT,
    HUD_M_CPUW,
    HUD_M_GPUW,
    HUD_M_COUNT
};

enum hud_graph_id_bi
{
    HUD_G_NONE = -1,
    HUD_G_MS = 0,
    HUD_G_PERCENT,
    HUD_G_MEMORY,
    HUD_G_POWER,
    HUD_G_COUNT
};

struct hud_metric_bi
{
    const char *label;
    const char *unit;
    hud_color_bi color;
    hud_graph_id_bi graph;
    bool show;
    bool graphed;
    bool available;
    hud_series_bi series;

    hud_metric_bi() : label("---:"), unit(""), color(HUD_COLOR_WHITE),
                      graph(HUD_G_NONE), show(false), graphed(false),
                      available(true) {}
};

struct hud_element_bi
{
    hud_element_kind_bi kind;
    hud_row_bi row;
    hud_graph_id_bi graph;

    hud_element_bi() : kind(HUD_EL_ROW), graph(HUD_G_NONE) {}
};

class hud_bi
{
public:
    hud_bi();

    hud_metric_bi metrics[HUD_M_COUNT];

    bool showDevice = true;
    bool showDisplay = true;
    bool showMem = true;

    bool showLows = true;

    double low1PercentFps = 0.0;
    double low01PercentFps = 0.0;
    bool low1Valid = false;
    bool low01Valid = false;

    enum bound_bi
    {
        BOUND_UNKNOWN = 0,
        BOUND_CPU,
        BOUND_GPU,
        BOUND_MIXED
    };

    bool showEfficiency = false;
    bool showBottleneck = false;
    bool showChargerDeficit = true;

    double efficiencyFpsPerWatt = 0.0;
    bool efficiencyValid = false;

    bound_bi bottleneck = BOUND_UNKNOWN;
    double bottleneckRatio = 0.0;

    double chargerDeficitW = 0.0;
    bool chargerDeficit = false;

    bool capturing = false;
    double captureSeconds = 0.0;
    size_t captureFrames = 0;

    bool showVram = false;
    bool vramAvailable = false;
    double vramUsedMB = 0.0;
    double vramTotalMB = 0.0;

    std::string cpuName;
    std::string cpuArch;
    bool showCpuName = false;
    bool showCpuArch = false;

    void initStaticInfo(const std::string &adapterName);

    void updateForeground(HWND foreground);

    void setApi(const char *apiName);
    void setMemory(double usedGB, double totalGB);

    void push(hud_metric_id_bi id, double value) { metrics[id].series.push(value); }

    void clearExtraRows();
    void addExtraRow(const std::string &label, const std::string &value);

    std::vector<hud_element_bi> buildLayout() const;

    bool graphHasContent(hud_graph_id_bi g) const;

    int columns() const;
    int columnsFor(const std::vector<hud_element_bi> &layout) const;

    void buildLayoutInto(std::vector<hud_element_bi> &out) const;

private:
    std::string deviceName;
    std::string resolution;
    std::string scaleLine;
    std::string refreshLine;
    std::string apiLabel;

    struct extra_row_bi
    {
        std::string left;
        std::string right;
    };

    std::vector<extra_row_bi> extraRows;

    double memUsedGB;
    double memTotalGB;

    HMONITOR cachedMonitor = NULL;
    DWORD cachedMonitorTick = 0;
    long cachedMonW = 0;
    long cachedMonH = 0;
    bool cachedDwmOn = false;
    unsigned long cachedHz = 0;

    long cachedWinW = -1;
    long cachedWinH = -1;
    double cachedScale = -1.0;
    bool cachedComposited = false;
    std::string cachedApiLabel;
};

#endif
