#include "hud_bi.h"

#include <dwmapi.h>
#include <cstdio>
#include <algorithm>

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

    metrics[HUD_M_CPUW].label = "PWR:";
    metrics[HUD_M_CPUW].unit = "W";
    metrics[HUD_M_CPUW].color = HUD_COLOR_ORANGE;
    metrics[HUD_M_CPUW].graph = HUD_G_POWER;
    metrics[HUD_M_CPUW].show = true;

    metrics[HUD_M_COMMIT].label = "Cmt:";
    metrics[HUD_M_COMMIT].unit = "%";
    metrics[HUD_M_COMMIT].color = HUD_COLOR_GREEN;
    metrics[HUD_M_COMMIT].graph = HUD_G_MEMORY;
    metrics[HUD_M_COMMIT].graphed = true;
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
    char buf[128];

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
            snprintf(buf, sizeof(buf), "[%luHz]", hz);
            refreshLine = buf;
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

        snprintf(buf, sizeof(buf), "[%ldx%ld]", winW, winH);
        resolution = buf;
    }

    double scale = (monW > 0) ? (double)winW / (double)monW : 1.0;

    if (scale != cachedScale || composited != cachedComposited ||
        apiLabel != cachedApiLabel || scaleLine.empty())
    {
        cachedScale = scale;
        cachedComposited = composited;
        cachedApiLabel = apiLabel;

        snprintf(buf, sizeof(buf), "%.2fx %s %s",
                 scale,
                 composited ? "Composited" : "Direct",
                 apiLabel.c_str());
        scaleLine = buf;
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

void hud_bi::addExtraRow(const std::string &text)
{
    if (text.size() > HUD_MAX_COLUMNS)
        extraRows.push_back(text.substr(0, HUD_MAX_COLUMNS));
    else
        extraRows.push_back(text);
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

static void formatMetricLeft(const hud_metric_bi &m, char *buf, size_t n)
{
    if (!m.available || m.series.size() == 0)
        snprintf(buf, n, "%-4s %6s %-2s", m.label, "-", m.unit);
    else
        snprintf(buf, n, "%-4s %6.2f %-2s", m.label,
                 clampDisplay(m.series.current()), m.unit);
}

static void formatMetricRight(const hud_metric_bi &m, char *buf, size_t n)
{
    if (!m.available || m.series.size() == 0)
        snprintf(buf, n, "[%6s %6s]", "-", "-");
    else
        snprintf(buf, n, "[%6.2f %6.2f]",
                 clampDisplay(m.series.minimum()), clampDisplay(m.series.maximum()));
}

static hud_element_bi &nextElement(std::vector<hud_element_bi> &out, size_t &n)
{
    if (n >= out.size())
        out.resize(n + 1);

    return out[n++];
}

static void pushRow(std::vector<hud_element_bi> &out, size_t &n, const char *left,
                    const char *right, hud_color_bi color)
{
    hud_element_bi &el = nextElement(out, n);
    el.kind = HUD_EL_ROW;
    el.row.left = left;
    el.row.right = right;
    el.row.color = color;
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

    char left[64];
    char right[64];

    if (showDevice)
        pushRow(out, n, deviceName, resolution, HUD_COLOR_WHITE);

    if (showDisplay)
        pushRow(out, n, scaleLine, refreshLine, HUD_COLOR_WHITE);

    for (int g = 0; g < HUD_G_COUNT; ++g)
    {
        hud_graph_id_bi group = (hud_graph_id_bi)g;

        for (int i = 0; i < HUD_M_COUNT; ++i)
        {
            const hud_metric_bi &m = metrics[i];
            if (m.graph != group || !m.show)
                continue;

            formatMetricLeft(m, left, sizeof(left));
            formatMetricRight(m, right, sizeof(right));
            pushRow(out, n, left, right, m.color);

            if (i == HUD_M_FPS && showLows && m.series.size() > 0)
            {
                if (m.available)
                {
                    snprintf(right, sizeof(right), "[%6.2f %6.2f]",
                             clampDisplay(m.series.percentile(0.01)),
                             clampDisplay(m.series.percentile(0.001)));
                }
                else
                {
                    snprintf(right, sizeof(right), "[%6s %6s]", "-", "-");
                }

                pushRow(out, n, "Low 1%/0.1%", right, HUD_COLOR_WHITE);
            }
        }

        if (graphHasContent(group))
        {
            hud_element_bi &el = nextElement(out, n);
            el.kind = HUD_EL_GRAPH;
            el.graph = group;
            el.row.left.clear();
            el.row.right.clear();
        }
    }

    for (int i = 0; i < HUD_M_COUNT; ++i)
    {
        const hud_metric_bi &m = metrics[i];
        if (m.graph != HUD_G_NONE || !m.show)
            continue;

        formatMetricLeft(m, left, sizeof(left));
        formatMetricRight(m, right, sizeof(right));
        pushRow(out, n, left, right, m.color);
    }

    if (showMem)
    {
        snprintf(left, sizeof(left), "%-4s %5.2fGB", "Mem:", memUsedGB);
        snprintf(right, sizeof(right), "[%.2fGB]", memTotalGB);
        pushRow(out, n, left, right, HUD_COLOR_WHITE);
    }

    for (size_t i = 0; i < extraRows.size(); ++i)
        pushRow(out, n, extraRows[i].c_str(), "", HUD_COLOR_WHITE);

    out.resize(n);
}
