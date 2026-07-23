#include "hud_bi.h"

#include <dwmapi.h>
#include <cstdio>
#include <algorithm>
#include <format>

hud_series_bi::hud_series_bi(size_t capacity)
    : data(capacity > 0 ? capacity : 1, 0.0),
      head(0),
      count(0),
      curValue(0.0),
      minValue(0.0),
      maxValue(0.0)
{
    scratch.reserve(data.size());
}

void hud_series_bi::reset()
{
    head = 0;
    count = 0;
    curValue = 0.0;
    minValue = 0.0;
    maxValue = 0.0;
}

void hud_series_bi::push(double value)
{
    data[head] = value;
    head = (head + 1) % data.size();

    if (count < data.size())
        ++count;

    curValue = value;

    minValue = value;
    maxValue = value;

    for (size_t i = 0; i < count; ++i)
    {
        double v = at(i);
        if (v < minValue)
            minValue = v;
        if (v > maxValue)
            maxValue = v;
    }
}

double hud_series_bi::percentile(double p) const
{
    if (count == 0)
        return 0.0;

    scratch.resize(count);
    for (size_t i = 0; i < count; ++i)
        scratch[i] = at(i);

    if (p < 0.0)
        p = 0.0;
    if (p > 1.0)
        p = 1.0;

    size_t idx = (size_t)(p * (double)(count - 1));

    std::nth_element(scratch.begin(), scratch.begin() + idx, scratch.end());
    return scratch[idx];
}

double hud_series_bi::at(size_t i) const
{
    if (i >= count)
        return 0.0;

    size_t oldest = (count < data.size()) ? 0 : head;
    return data[(oldest + i) % data.size()];
}

hud_bi::hud_bi()
    : deviceName("Unknown GPU"),
      resolution("[0x0]"),
      scaleLine("1.0x"),
      refreshLine("60Hz"),
      apiLabel("-"),
      memUsedGB(0.0),
      memTotalGB(0.0)
{
    metrics[HUD_M_FPS].label = "FPS:";
    metrics[HUD_M_FPS].unit = "";
    metrics[HUD_M_FPS].color = HUD_COLOR_WHITE;
    metrics[HUD_M_FPS].graph = HUD_G_MS;
    metrics[HUD_M_FPS].show = true;

    metrics[HUD_M_PRE].label = "Pre:";
    metrics[HUD_M_PRE].unit = "ms";
    metrics[HUD_M_PRE].color = HUD_COLOR_BLUE;
    metrics[HUD_M_PRE].graph = HUD_G_MS;
    metrics[HUD_M_PRE].show = true;
    metrics[HUD_M_PRE].graphed = true;

    metrics[HUD_M_GPUMS].label = "GPU:";
    metrics[HUD_M_GPUMS].unit = "ms";
    metrics[HUD_M_GPUMS].color = HUD_COLOR_RED;
    metrics[HUD_M_GPUMS].graph = HUD_G_MS;
    metrics[HUD_M_GPUMS].show = true;
    metrics[HUD_M_GPUMS].graphed = true;

    metrics[HUD_M_CPU].label = "CPU:";
    metrics[HUD_M_CPU].unit = "%";
    metrics[HUD_M_CPU].color = HUD_COLOR_BLUE;
    metrics[HUD_M_CPU].graph = HUD_G_PERCENT;
    metrics[HUD_M_CPU].graphed = true;

    metrics[HUD_M_GPU].label = "GPU:";
    metrics[HUD_M_GPU].unit = "%";
    metrics[HUD_M_GPU].color = HUD_COLOR_GREEN;
    metrics[HUD_M_GPU].graph = HUD_G_PERCENT;
    metrics[HUD_M_GPU].graphed = true;

    metrics[HUD_M_RAM].label = "RAM:";
    metrics[HUD_M_RAM].unit = "%";
    metrics[HUD_M_RAM].color = HUD_COLOR_MAGENTA;
    metrics[HUD_M_RAM].graph = HUD_G_MEMORY;
    metrics[HUD_M_RAM].graphed = true;

    metrics[HUD_M_CPUW].label = "CPW:";
    metrics[HUD_M_CPUW].unit = "W";
    metrics[HUD_M_CPUW].color = HUD_COLOR_ORANGE;
    metrics[HUD_M_CPUW].graph = HUD_G_POWER;
    metrics[HUD_M_CPUW].show = true;

    metrics[HUD_M_GPUW].label = "GPW:";
    metrics[HUD_M_GPUW].unit = "W";
    metrics[HUD_M_GPUW].color = HUD_COLOR_CYAN;
    metrics[HUD_M_GPUW].graph = HUD_G_POWER;
    metrics[HUD_M_GPUW].show = false;

    metrics[HUD_M_COMMIT].label = "Cmt:";
    metrics[HUD_M_COMMIT].unit = "%";
    metrics[HUD_M_COMMIT].color = HUD_COLOR_GREEN;
    metrics[HUD_M_COMMIT].graph = HUD_G_MEMORY;
    metrics[HUD_M_COMMIT].graphed = true;

    metrics[HUD_M_BATTERYD].label = "Bat:";
    metrics[HUD_M_BATTERYD].unit = "W";
    metrics[HUD_M_BATTERYD].color = HUD_COLOR_CYAN;
    metrics[HUD_M_BATTERYD].graph = HUD_G_BATTERY;
    metrics[HUD_M_BATTERYD].show = false;
    metrics[HUD_M_BATTERYD].graphed = true;

    metrics[HUD_M_NETDOWN].label = "Dw:";
    metrics[HUD_M_NETDOWN].unit = "KB/s";
    metrics[HUD_M_NETDOWN].color = HUD_COLOR_CYAN;
    metrics[HUD_M_NETDOWN].graph = HUD_G_NONE;
    metrics[HUD_M_NETDOWN].show = false;

    metrics[HUD_M_NETUP].label = "Up:";
    metrics[HUD_M_NETUP].unit = "KB/s";
    metrics[HUD_M_NETUP].color = HUD_COLOR_MAGENTA;
    metrics[HUD_M_NETUP].graph = HUD_G_NONE;
    metrics[HUD_M_NETUP].show = false;

    metrics[HUD_M_DISK].label = "Dsk:";
    metrics[HUD_M_DISK].unit = "%";
    metrics[HUD_M_DISK].color = HUD_COLOR_ORANGE;
    metrics[HUD_M_DISK].graph = HUD_G_NONE;
    metrics[HUD_M_DISK].show = false;
}

void hud_bi::initStaticInfo(const std::string &adapterName)
{
    deviceName = adapterName.empty() ? "Unknown GPU" : adapterName;

    size_t maxName = 0;
    if (resolution.size() + 1 < HUD_MAX_COLUMNS)
        maxName = HUD_MAX_COLUMNS - resolution.size() - 1;

    if (deviceName.size() > maxName)
        deviceName.resize(maxName);
}

void hud_bi::setApi(const char *name)
{
    apiLabel = (name && *name) ? name : "-";
}

void hud_bi::updateForeground(HWND foreground)
{
    HMONITOR mon = MonitorFromWindow(foreground ? foreground : GetDesktopWindow(),
                                     MONITOR_DEFAULTTOPRIMARY);

    DWORD now = GetTickCount();
    bool monitorStale = (mon != cachedMonitor) || (cachedMonitorTick == 0) ||
                        ((now - cachedMonitorTick) > 2000);

    if (monitorStale)
    {
        cachedMonitor = mon;
        cachedMonitorTick = now;

        MONITORINFOEXA mi;
        ZeroMemory(&mi, sizeof(mi));
        mi.cbSize = sizeof(mi);

        cachedMonW = GetSystemMetrics(SM_CXSCREEN);
        cachedMonH = GetSystemMetrics(SM_CYSCREEN);

        if (mon && GetMonitorInfoA(mon, &mi))
        {
            cachedMonW = mi.rcMonitor.right - mi.rcMonitor.left;
            cachedMonH = mi.rcMonitor.bottom - mi.rcMonitor.top;
        }

        BOOL dwmOn = FALSE;
        cachedDwmOn = SUCCEEDED(DwmIsCompositionEnabled(&dwmOn)) && dwmOn;

        DEVMODEA dm;
        ZeroMemory(&dm, sizeof(dm));
        dm.dmSize = sizeof(dm);

        const char *deviceForMode = (mon && mi.szDevice[0]) ? mi.szDevice : NULL;

        unsigned long hz = 60;
        if (EnumDisplaySettingsA(deviceForMode, ENUM_CURRENT_SETTINGS, &dm) &&
            dm.dmDisplayFrequency > 1)
        {
            hz = (unsigned long)dm.dmDisplayFrequency;
        }

        if (hz != cachedHz || refreshLine.empty())
        {
            cachedHz = hz;
            refreshLine = std::format("[{}Hz]", hz);
        }
    }

    long monW = cachedMonW;
    long monH = cachedMonH;

    long winW = monW;
    long winH = monH;

    RECT client;
    if (foreground && GetClientRect(foreground, &client))
    {
        long w = client.right - client.left;
        long h = client.bottom - client.top;
        if (w > 0 && h > 0)
        {
            winW = w;
            winH = h;
        }
    }

    bool composited = cachedDwmOn;

    if (composited && foreground)
    {
        RECT wr;
        if (GetWindowRect(foreground, &wr) &&
            (wr.right - wr.left) >= monW && (wr.bottom - wr.top) >= monH)
        {
            composited = false;
        }
    }

    if (winW != cachedWinW || winH != cachedWinH)
    {
        cachedWinW = winW;
        cachedWinH = winH;

        resolution = std::format("[{}x{}]", winW, winH);
    }

    double scale = (monW > 0) ? (double)winW / (double)monW : 1.0;

    if (scale != cachedScale || composited != cachedComposited ||
        apiLabel != cachedApiLabel || scaleLine.empty())
    {
        cachedScale = scale;
        cachedComposited = composited;
        cachedApiLabel = apiLabel;

        scaleLine = std::format("{:.2f}x {} {}",
                                scale,
                                composited ? "Composited" : "Direct",
                                apiLabel);
    }
}

void hud_bi::setMemory(double usedGB, double totalGB)
{
    memUsedGB = usedGB;
    memTotalGB = totalGB;
}

void hud_bi::clearExtraRows()
{
    extraRows.clear();
}

void hud_bi::addExtraRow(const std::string &label, const std::string &value)
{
    extra_row_bi row;
    row.left = label.substr(0, HUD_MAX_COLUMNS);

    size_t room = (label.size() + 1 < HUD_MAX_COLUMNS) ? (HUD_MAX_COLUMNS - label.size() - 1) : 0;
    row.right = value.substr(0, room);

    extraRows.push_back(row);
}

bool hud_bi::graphHasContent(hud_graph_id_bi g) const
{
    for (int i = 0; i < HUD_M_COUNT; ++i)
    {
        const hud_metric_bi &m = metrics[i];
        if (m.graph == g && m.show && m.graphed && m.series.size() > 0)
            return true;
    }
    return false;
}

int hud_bi::columns() const
{
    std::vector<hud_element_bi> layout;
    buildLayoutInto(layout);
    return columnsFor(layout);
}

int hud_bi::columnsFor(const std::vector<hud_element_bi> &layout) const
{
    size_t widest = HUD_MIN_COLUMNS;

    for (size_t i = 0; i < layout.size(); ++i)
    {
        if (layout[i].kind != HUD_EL_ROW)
            continue;

        const hud_row_bi &r = layout[i].row;
        size_t need = r.left.size() + r.right.size();

        if (!r.left.empty() && !r.right.empty())
            need += 1;

        if (need > widest)
            widest = need;
    }

    if (widest > HUD_MAX_COLUMNS)
        widest = HUD_MAX_COLUMNS;

    return (int)widest;
}

static double clampDisplay(double v)
{
    if (v > 999.99)
        return 999.99;
    if (v < -999.99)
        return -999.99;
    return v;
}

static double clampField(double value, int decimals)
{
    double high = 999999.0;
    double low = -99999.0;

    if (decimals == 1)
    {
        high = 9999.9;
        low = -999.9;
    }
    else if (decimals >= 2)
    {
        high = 999.99;
        low = -99.99;
    }

    if (value > high)
        return high;
    if (value < low)
        return low;

    return value;
}

static std::string formatFixed(double value, int decimals)
{
    double v = clampField(value, decimals);

    if (decimals <= 0)
        return std::format("{:.0f}", v);
    if (decimals == 1)
        return std::format("{:.1f}", v);
    return std::format("{:.2f}", v);
}

static std::string fmtLeft(const char *label, const char *value, const char *unit)
{
    return std::format("{:<4} {:>6} {:<2}", label, value, unit);
}

static std::string fmtLeftNum(const char *label, double value, int decimals, const char *unit)
{
    return fmtLeft(label, formatFixed(value, decimals).c_str(), unit);
}

static std::string fmtRight(const char *a, const char *b)
{
    return std::format("[{:>6} {:>6}]", a, b);
}

static std::string fmtRightPhrase(const char *phrase)
{
    return std::format("[{:>13}]", phrase);
}

static std::string fmtRightTotalPct(double total, int decimals, double percent)
{
    std::string text = formatFixed(total, decimals);

    if (percent > 999.9)
        percent = 999.9;
    if (percent < 0.0)
        percent = 0.0;

    return std::format("[{:>6} {:5.1f}%]", text, percent);
}

static std::string formatMetricLeft(const hud_metric_bi &m)
{
    if (!m.available || m.series.size() == 0)
        return std::format("{:<4} {:>6} {:<2}", m.label, "-", m.unit);
    return std::format("{:<4} {:6.2f} {:<2}", m.label,
                       clampDisplay(m.series.current()), m.unit);
}

static std::string formatMetricRight(const hud_metric_bi &m)
{
    if (!m.available || m.series.size() == 0)
        return std::format("[{:>6} {:>6}]", "-", "-");
    return std::format("[{:6.2f} {:6.2f}]",
                       clampDisplay(m.series.minimum()), clampDisplay(m.series.maximum()));
}

static hud_element_bi &nextElement(std::vector<hud_element_bi> &out, size_t &n)
{
    if (n >= out.size())
        out.resize(n + 1);

    return out[n++];
}

static void pushRow(std::vector<hud_element_bi> &out, size_t &n, const std::string &left,
                    const std::string &right, hud_color_bi color)
{
    hud_element_bi &el = nextElement(out, n);
    el.kind = HUD_EL_ROW;
    el.row.left = left;
    el.row.right = right;
    el.row.color = color;
}

std::vector<hud_element_bi> hud_bi::buildLayout() const
{
    std::vector<hud_element_bi> out;
    buildLayoutInto(out);
    return out;
}

void hud_bi::buildLayoutInto(std::vector<hud_element_bi> &out) const
{
    size_t n = 0;

    auto eachMetric = [&](auto fn) {
        if (metricOrder.empty())
        {
            for (int i = 0; i < HUD_M_COUNT; ++i)
                fn(i);
        }
        else
        {
            for (size_t oi = 0; oi < metricOrder.size(); ++oi)
                if (metricOrder[oi] >= 0 && metricOrder[oi] < HUD_M_COUNT)
                    fn(metricOrder[oi]);
        }
    };

    if (capturing)
    {
        int mins = (int)(captureSeconds / 60.0);
        int secs = (int)captureSeconds % 60;

        std::string clock = std::format("{}:{:02d}", mins, secs);
        std::string frames = std::format("{}", (unsigned)captureFrames);

        pushRow(out, n, fmtLeft("REC:", clock.c_str(), ""),
                fmtRight(frames.c_str(), "frames"), HUD_COLOR_RED);
    }

    if (showCpuName && !cpuName.empty())
        pushRow(out, n, cpuName, "", HUD_COLOR_WHITE);

    if (showCpuArch && !cpuArch.empty())
        pushRow(out, n, "Arch: " + cpuArch, "", HUD_COLOR_WHITE);

    if (showDevice)
        pushRow(out, n, deviceName, resolution, HUD_COLOR_WHITE);

    if (showDisplay)
        pushRow(out, n, scaleLine, refreshLine, HUD_COLOR_WHITE);

    for (int g = 0; g < HUD_G_COUNT; ++g)
    {
        hud_graph_id_bi group = (hud_graph_id_bi)g;

        eachMetric([&](int i) {
            const hud_metric_bi &m = metrics[i];
            if (m.graph != group || !m.show)
                return;

            pushRow(out, n, formatMetricLeft(m), formatMetricRight(m), m.color);

            if (i == HUD_M_FPS && showLows)
            {
                std::string one = (m.available && low1Valid)
                    ? formatFixed(clampDisplay(low1PercentFps), 1) : "-";

                std::string tenth = (m.available && low01Valid)
                    ? formatFixed(clampDisplay(low01PercentFps), 1) : "-";

                pushRow(out, n, fmtLeft("Low:", "1/0.1", "%"),
                        fmtRight(one.c_str(), tenth.c_str()), HUD_COLOR_WHITE);
            }

            if (i == HUD_M_FPS && showBottleneck)
            {
                bool known = m.available && bottleneck != BOUND_UNKNOWN;
                hud_color_bi tone = HUD_COLOR_WHITE;
                const char *phrase = "-";

                switch (bottleneck)
                {
                case BOUND_GPU:
                    phrase = "GPU bound";
                    tone = HUD_COLOR_GREEN;
                    break;
                case BOUND_CPU:
                    phrase = "CPU bound";
                    tone = HUD_COLOR_BLUE;
                    break;
                case BOUND_MIXED:
                    phrase = "balanced";
                    break;
                default:
                    break;
                }

                if (known)
                    pushRow(out, n, fmtLeftNum("Bnd:", bottleneckRatio * 100.0, 0, "%"),
                            fmtRightPhrase(phrase), tone);
                else
                    pushRow(out, n, fmtLeft("Bnd:", "-", ""),
                            fmtRightPhrase(phrase), HUD_COLOR_WHITE);
            }

            if (i == HUD_M_CPUW && showEfficiency)
            {
                std::string left = efficiencyValid
                    ? fmtLeftNum("Eff:", efficiencyFpsPerWatt, 2, "")
                    : fmtLeft("Eff:", "-", "");

                pushRow(out, n, left, fmtRightPhrase("frames per W"), HUD_COLOR_ORANGE);
            }
        });

        if (graphHasContent(group))
        {
            hud_element_bi &el = nextElement(out, n);
            el.kind = HUD_EL_GRAPH;
            el.graph = group;
            el.row.left.clear();
            el.row.right.clear();
        }
    }

    eachMetric([&](int i) {
        const hud_metric_bi &m = metrics[i];
        if (m.graph != HUD_G_NONE || !m.show)
            return;

        pushRow(out, n, formatMetricLeft(m), formatMetricRight(m), m.color);
    });

    if (showMem)
    {
        if (memTotalGB > 0.0)
            pushRow(out, n, fmtLeftNum("Mem:", memUsedGB, 2, "GB"),
                    fmtRightTotalPct(memTotalGB, 2, (memUsedGB / memTotalGB) * 100.0),
                    HUD_COLOR_WHITE);
        else
            pushRow(out, n, fmtLeftNum("Mem:", memUsedGB, 2, "GB"),
                    fmtRight("-", "-"), HUD_COLOR_WHITE);
    }

    if (showVram)
    {
        if (vramAvailable)
        {
            if (vramTotalMB > 0.0)
                pushRow(out, n, fmtLeftNum("VRM:", vramUsedMB, 0, "MB"),
                        fmtRightTotalPct(vramTotalMB, 0,
                                         (vramUsedMB / vramTotalMB) * 100.0),
                        HUD_COLOR_GREEN);
            else
                pushRow(out, n, fmtLeftNum("VRM:", vramUsedMB, 0, "MB"),
                        fmtRight("-", "-"), HUD_COLOR_GREEN);
        }
        else
        {
            pushRow(out, n, fmtLeft("VRM:", "-", "MB"),
                    fmtRight("-", "-"), HUD_COLOR_GREEN);
        }
    }

    if (showChargerDeficit && chargerDeficit)
    {
        pushRow(out, n, fmtLeftNum("Chg:", chargerDeficitW, 1, "W"),
                fmtRightPhrase("charger short"), HUD_COLOR_RED);
    }

    if (showNetwork)
    {
        const hud_metric_bi &down = metrics[HUD_M_NETDOWN];
        const hud_metric_bi &up = metrics[HUD_M_NETUP];
        if (down.show && down.series.size() > 0)
            pushRow(out, n, formatMetricLeft(down), formatMetricRight(down), down.color);
        if (up.show && up.series.size() > 0)
            pushRow(out, n, formatMetricLeft(up), formatMetricRight(up), up.color);
    }

    if (showDisk && diskAvailable)
    {
        if (diskTotalGB > 0.0)
            pushRow(out, n, fmtLeftNum("Dsk:", diskUsedGB, 1, "GB"),
                    fmtRightTotalPct(diskTotalGB, 1, (diskUsedGB / diskTotalGB) * 100.0),
                    HUD_COLOR_ORANGE);
        else
            pushRow(out, n, fmtLeftNum("Dsk:", diskUsedGB, 1, "GB"),
                    fmtRight("-", "-"), HUD_COLOR_ORANGE);
    }

    eachMetric([&](int i) {
        const hud_metric_bi &m = metrics[i];
        if (m.graph != HUD_G_BATTERY || !m.show)
            return;

        pushRow(out, n, formatMetricLeft(m), formatMetricRight(m), m.color);
    });

    if (graphHasContent(HUD_G_BATTERY))
    {
        hud_element_bi &el = nextElement(out, n);
        el.kind = HUD_EL_GRAPH;
        el.graph = HUD_G_BATTERY;
        el.row.left.clear();
        el.row.right.clear();
    }

    for (size_t i = 0; i < extraRows.size(); ++i)
        pushRow(out, n, extraRows[i].left, extraRows[i].right, HUD_COLOR_WHITE);

    out.resize(n);
}
