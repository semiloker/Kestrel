#include "settings_bi.h"

#include "resource_usage_bi.h"
#include "overlay_bi.h"
#include "draw_batteryinfo_bi.h"
#include "BatteryInfo.h"
#include "paths_bi.h"
#include "logger_bi.h"
#include "app_identity_bi.h"

#include <cstdio>
#include <cstdlib>

namespace
{
    const char *SETTINGS_FILE = "settings.ini";

    const char *METRIC_KEYS[HUD_M_COUNT] = {
        "fps", "pre", "gpums", "cpu", "gpu", "ram", "commit", "cpuw", "gpuw"};

    void trim(std::string &s)
    {
        size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos)
        {
            s.clear();
            return;
        }
        size_t e = s.find_last_not_of(" \t\r\n");
        s = s.substr(b, e - b + 1);
    }
}

void settings_bi::load()
{
    values.clear();

    std::string path = paths_bi::inDataDir(SETTINGS_FILE);
    if (path.empty())
        return;

    FILE *f = fopen(path.c_str(), "r");
    if (!f)
    {
        log_bi::write("settings: no saved file yet, using defaults");
        return;
    }

    char line[512];
    while (fgets(line, sizeof(line), f))
    {
        std::string s(line);

        trim(s);
        if (s.empty() || s[0] == '#' || s[0] == ';')
            continue;

        size_t eq = s.find('=');
        if (eq == std::string::npos)
            continue;

        std::string key = s.substr(0, eq);
        std::string value = s.substr(eq + 1);
        trim(key);
        trim(value);

        if (!key.empty())
            values[key] = value;
    }

    fclose(f);
    log_bi::write("settings: loaded %u values", (unsigned)values.size());
}

bool settings_bi::save() const
{
    std::string path = paths_bi::inDataDir(SETTINGS_FILE);
    if (path.empty())
        return false;

    FILE *f = fopen(path.c_str(), "w");
    if (!f)
    {
        log_bi::write("settings: cannot open %s for writing", path.c_str());
        return false;
    }

    fprintf(f, "# " APP_NAME " settings. Delete this file to reset everything.\n");

    for (std::map<std::string, std::string>::const_iterator it = values.begin();
         it != values.end(); ++it)
    {
        fprintf(f, "%s=%s\n", it->first.c_str(), it->second.c_str());
    }

    fclose(f);
    return true;
}

bool settings_bi::getBool(const char *key, bool def) const
{
    std::map<std::string, std::string>::const_iterator it = values.find(key);
    if (it == values.end())
        return def;

    return it->second == "1" || it->second == "true" || it->second == "yes";
}

int settings_bi::getInt(const char *key, int def) const
{
    std::map<std::string, std::string>::const_iterator it = values.find(key);
    if (it == values.end())
        return def;

    return atoi(it->second.c_str());
}

float settings_bi::getFloat(const char *key, float def) const
{
    std::map<std::string, std::string>::const_iterator it = values.find(key);
    if (it == values.end())
        return def;

    return (float)atof(it->second.c_str());
}

std::string settings_bi::getString(const char *key, const std::string &def) const
{
    std::map<std::string, std::string>::const_iterator it = values.find(key);
    if (it == values.end())
        return def;

    return it->second;
}

void settings_bi::setBool(const char *key, bool value)
{
    values[key] = value ? "1" : "0";
}

void settings_bi::setString(const char *key, const std::string &value)
{
    values[key] = value;
}

void settings_bi::setInt(const char *key, int value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    values[key] = buf;
}

void settings_bi::setFloat(const char *key, float value)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%.4f", value);
    values[key] = buf;
}

void settings_bi::applyTo(resource_usage_bi *ru, overlay_bi *ov,
                          draw_batteryinfo_bi *draw, batteryinfo_bi *bi) const
{
    if (bi)
    {
        bi->info_1s.Voltage_ = getBool("battery.voltage", bi->info_1s.Voltage_);
        bi->info_1s.Rate_ = getBool("battery.rate", bi->info_1s.Rate_);
        bi->info_1s.PowerState_ = getBool("battery.powerState", bi->info_1s.PowerState_);
        bi->info_1s.RemainingCapacity_ = getBool("battery.remaining", bi->info_1s.RemainingCapacity_);
        bi->info_1s.ChargeLevel_ = getBool("battery.charge", bi->info_1s.ChargeLevel_);
        bi->info_10s.TimeRemaining_ = getBool("battery.timeLeft", bi->info_10s.TimeRemaining_);
    }

    if (ru)
    {
        ru->cpuInfo.show_cpuName = getBool("cpu.name", ru->cpuInfo.show_cpuName);
        ru->cpuInfo.show_architecture = getBool("cpu.arch", ru->cpuInfo.show_architecture);
        ru->cpuInfo.show_UsagePercent = getBool("cpu.usage", ru->cpuInfo.show_UsagePercent);
        ru->cpuInfo.show_CoreUsagePercents = getBool("cpu.cores", ru->cpuInfo.show_CoreUsagePercents);
        ru->cpuInfo.show_packagePower = getBool("cpu.power", ru->cpuInfo.show_packagePower);

        ru->ramInfo.show_dwMemoryLoad = getBool("ram.load", ru->ramInfo.show_dwMemoryLoad);
        ru->ramInfo.show_ullTotalPhys = getBool("ram.total", ru->ramInfo.show_ullTotalPhys);
        ru->ramInfo.show_ullAvailPhys = getBool("ram.avail", ru->ramInfo.show_ullAvailPhys);
        ru->ramInfo.show_ullTotalPageFile = getBool("ram.commit", ru->ramInfo.show_ullTotalPageFile);
        ru->ramInfo.show_ullAvailPageFile = getBool("ram.availpage", ru->ramInfo.show_ullAvailPageFile);
        ru->ramInfo.show_ullTotalVirtual = getBool("ram.totalvirt", ru->ramInfo.show_ullTotalVirtual);
        ru->ramInfo.show_ullAvailVirtual = getBool("ram.availvirt", ru->ramInfo.show_ullAvailVirtual);
        ru->ramInfo.show_ullAvailExtendedVirtual = getBool("ram.extvirt", ru->ramInfo.show_ullAvailExtendedVirtual);

        ru->gpuInfo.show_gpuName = getBool("gpu.name", ru->gpuInfo.show_gpuName);
        ru->gpuInfo.show_gpuLoad = getBool("gpu.load", ru->gpuInfo.show_gpuLoad);
        ru->gpuInfo.show_vram = getBool("gpu.vram", ru->gpuInfo.show_vram);
        ru->gpuInfo.show_gpuPower = getBool("gpu.power", ru->gpuInfo.show_gpuPower);

        int unit = getInt("mem.unit", ru->memUnit);
        if (unit >= resource_usage_bi::MEM_UNIT_AUTO && unit <= resource_usage_bi::MEM_UNIT_GB)
            ru->memUnit = unit;

        ru->minimize_To_Tray = getBool("app.minimizeToTray", ru->minimize_To_Tray);
        ru->exit_on_key_esc = getBool("app.exitOnEsc", ru->exit_on_key_esc);
    }

    if (ov)
    {
        ov->show_on_screen_display = getBool("hud.enabled", ov->show_on_screen_display);
        ov->margin = getInt("hud.margin", ov->margin);
        ov->setScale(getInt("hud.scale", ov->getScale()));

        int refresh = getInt("hud.refreshMs", ov->refreshMs);
        if (refresh >= 30 && refresh <= 2000)
            ov->refreshMs = refresh;

        int corner = getInt("hud.corner", (int)ov->corner);
        if (corner >= 0 && corner <= (int)overlay_bi::CORNER_BOTTOM_RIGHT)
            ov->corner = (overlay_bi::corner_bi)corner;

        for (int i = 0; i < HUD_M_COUNT; ++i)
        {
            std::string base = std::string("metric.") + METRIC_KEYS[i];
            ov->hud.metrics[i].show = getBool((base + ".show").c_str(), ov->hud.metrics[i].show);
            ov->hud.metrics[i].graphed = getBool((base + ".graph").c_str(), ov->hud.metrics[i].graphed);
        }

        ov->hud.showDevice = getBool("hud.device", ov->hud.showDevice);
        ov->hud.showDisplay = getBool("hud.display", ov->hud.showDisplay);
        ov->hud.showMem = getBool("hud.mem", ov->hud.showMem);
        ov->hud.showLows = getBool("hud.lows", ov->hud.showLows);
        ov->hud.showBottleneck = getBool("hud.bottleneck", ov->hud.showBottleneck);
        ov->hud.showEfficiency = getBool("hud.efficiency", ov->hud.showEfficiency);
        ov->hud.showChargerDeficit = getBool("hud.chargerDeficit", ov->hud.showChargerDeficit);
    }

    if (draw)
    {
        draw->setNightMode(getBool("ui.nightMode", draw->getNightMode()));
        draw->setSettingsGroup(getInt("ui.settingsGroup", draw->getSettingsGroup()));

        draw->userPreset[0] = getString("ui.userpreset1", draw->userPreset[0]);
        draw->userPreset[1] = getString("ui.userpreset2", draw->userPreset[1]);
        draw->userPreset[2] = getString("ui.userpreset3", draw->userPreset[2]);

        const D2D1_COLOR_F &cur = draw->getAccentColor();
        D2D1_COLOR_F accent;
        accent.r = getFloat("ui.accent.r", cur.r);
        accent.g = getFloat("ui.accent.g", cur.g);
        accent.b = getFloat("ui.accent.b", cur.b);
        accent.a = 1.0f;
        draw->setAccentColor(accent);
    }
}

void settings_bi::collectFrom(const resource_usage_bi *ru, const overlay_bi *ov,
                              const draw_batteryinfo_bi *draw, const batteryinfo_bi *bi)
{
    if (bi)
    {
        setBool("battery.voltage", bi->info_1s.Voltage_);
        setBool("battery.rate", bi->info_1s.Rate_);
        setBool("battery.powerState", bi->info_1s.PowerState_);
        setBool("battery.remaining", bi->info_1s.RemainingCapacity_);
        setBool("battery.charge", bi->info_1s.ChargeLevel_);
        setBool("battery.timeLeft", bi->info_10s.TimeRemaining_);
    }

    if (ru)
    {
        setBool("cpu.name", ru->cpuInfo.show_cpuName);
        setBool("cpu.arch", ru->cpuInfo.show_architecture);
        setBool("cpu.usage", ru->cpuInfo.show_UsagePercent);
        setBool("cpu.cores", ru->cpuInfo.show_CoreUsagePercents);
        setBool("cpu.power", ru->cpuInfo.show_packagePower);

        setBool("ram.load", ru->ramInfo.show_dwMemoryLoad);
        setBool("ram.total", ru->ramInfo.show_ullTotalPhys);
        setBool("ram.avail", ru->ramInfo.show_ullAvailPhys);
        setBool("ram.commit", ru->ramInfo.show_ullTotalPageFile);
        setBool("ram.availpage", ru->ramInfo.show_ullAvailPageFile);
        setBool("ram.totalvirt", ru->ramInfo.show_ullTotalVirtual);
        setBool("ram.availvirt", ru->ramInfo.show_ullAvailVirtual);
        setBool("ram.extvirt", ru->ramInfo.show_ullAvailExtendedVirtual);

        setBool("gpu.name", ru->gpuInfo.show_gpuName);
        setBool("gpu.load", ru->gpuInfo.show_gpuLoad);
        setBool("gpu.vram", ru->gpuInfo.show_vram);
        setBool("gpu.power", ru->gpuInfo.show_gpuPower);

        setInt("mem.unit", ru->memUnit);

        setBool("app.minimizeToTray", ru->minimize_To_Tray);
        setBool("app.exitOnEsc", ru->exit_on_key_esc);
    }

    if (ov)
    {
        setBool("hud.enabled", ov->show_on_screen_display);
        setInt("hud.margin", ov->margin);
        setInt("hud.scale", ov->getScale());
        setInt("hud.refreshMs", ov->refreshMs);
        setInt("hud.corner", (int)ov->corner);

        for (int i = 0; i < HUD_M_COUNT; ++i)
        {
            std::string base = std::string("metric.") + METRIC_KEYS[i];
            setBool((base + ".show").c_str(), ov->hud.metrics[i].show);
            setBool((base + ".graph").c_str(), ov->hud.metrics[i].graphed);
        }

        setBool("hud.device", ov->hud.showDevice);
        setBool("hud.display", ov->hud.showDisplay);
        setBool("hud.mem", ov->hud.showMem);
        setBool("hud.lows", ov->hud.showLows);
        setBool("hud.bottleneck", ov->hud.showBottleneck);
        setBool("hud.efficiency", ov->hud.showEfficiency);
        setBool("hud.chargerDeficit", ov->hud.showChargerDeficit);
    }

    if (draw)
    {
        setBool("ui.nightMode", draw->getNightMode());
        setInt("ui.settingsGroup", draw->getSettingsGroup());

        const D2D1_COLOR_F &accent = draw->getAccentColor();
        setFloat("ui.accent.r", accent.r);
        setFloat("ui.accent.g", accent.g);
        setFloat("ui.accent.b", accent.b);

        setString("ui.userpreset1", draw->userPreset[0]);
        setString("ui.userpreset2", draw->userPreset[1]);
        setString("ui.userpreset3", draw->userPreset[2]);
    }
}
