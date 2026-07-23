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
#include <format>

namespace
{
    const char *SETTINGS_FILE = "settings.ini";

    // MUST have exactly HUD_M_COUNT entries — a missing key is a nullptr that
    // crashes applyTo/collectFrom via strlen(nullptr). Keep in sync with the
    // HUD_M_* enum in hud_bi.h.
    const char *METRIC_KEYS[HUD_M_COUNT] = {
        "fps", "pre", "gpums", "cpu", "gpu", "ram", "commit", "cpuw", "gpuw", "batteryd",
        "netdown", "netup", "disk"};
    static_assert(sizeof(METRIC_KEYS) / sizeof(METRIC_KEYS[0]) == HUD_M_COUNT,
                  "METRIC_KEYS must have one entry per HUD_M_* metric");

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

Result<void> settings_bi::load()
{
    values.clear();
    profiles.clear();
    activeProfile.clear();

    std::string path = paths_bi::inDataDir(SETTINGS_FILE);
    if (path.empty())
        return Result<void>(std::string("settings: data dir not available"));

    FILE *f = fopen(path.c_str(), "r");
    if (!f)
    {
        log_bi::write("settings: no saved file yet, using defaults");
        return Result<void>();
    }

    std::string currentSection;
    char line[512];
    int lineNum = 0;
    while (fgets(line, sizeof(line), f))
    {
        ++lineNum;
        std::string s(line);

        trim(s);
        if (s.empty() || s[0] == '#' || s[0] == ';')
            continue;

        if (s[0] == '[')
        {
            size_t close = s.find(']');
            if (close == std::string::npos)
                continue;
            currentSection = s.substr(1, close - 1);
            continue;
        }

        size_t eq = s.find('=');
        if (eq == std::string::npos)
        {
            log_bi::write("settings: ignoring line %d (no '=')", lineNum);
            continue;
        }

        std::string key = s.substr(0, eq);
        std::string value = s.substr(eq + 1);
        trim(key);
        trim(value);

        if (key.empty())
        {
            log_bi::write("settings: ignoring line %d (empty key)", lineNum);
            continue;
        }

        if (!currentSection.empty() && currentSection.find("Profile:") == 0)
        {
            profiles[currentSection.substr(8)][key] = value;  // strip "Profile:" -> bare exe name
        }
        else
        {
            values[key] = value;
        }
    }

    fclose(f);
    log_bi::write("settings: loaded %u global values, %zu profiles",
                  (unsigned)values.size(), profiles.size());
    return Result<void>();
}

Result<void> settings_bi::save() const
{
    std::string path = paths_bi::inDataDir(SETTINGS_FILE);
    if (path.empty())
        return Result<void>(std::string("settings: data dir not available"));

    FILE *f = fopen(path.c_str(), "w");
    if (!f)
    {
        std::string err = std::format("settings: cannot open {} for writing", path);
        log_bi::write(err.c_str());
        return Result<void>(err);
    }

    fprintf(f, "# " APP_NAME " settings. Delete this file to reset everything.\n");

    for (std::map<std::string, std::string>::const_iterator it = values.begin();
         it != values.end(); ++it)
    {
        if (it->first.compare(0, 9, "profile:#") == 0)
            continue;
        fprintf(f, "%s=%s\n", it->first.c_str(), it->second.c_str());
    }

    for (std::map<std::string, std::map<std::string, std::string>>::const_iterator pit = profiles.begin();
         pit != profiles.end(); ++pit)
    {
        fprintf(f, "\n[Profile:%s]\n", pit->first.c_str());
        for (std::map<std::string, std::string>::const_iterator it = pit->second.begin();
             it != pit->second.end(); ++it)
        {
            fprintf(f, "%s=%s\n", it->first.c_str(), it->second.c_str());
        }
    }

    fclose(f);
    return Result<void>();
}

std::string settings_bi::getValue(const char *key) const
{
    if (!activeProfile.empty())
    {
        std::map<std::string, std::map<std::string, std::string>>::const_iterator pit = profiles.find(activeProfile);
        if (pit != profiles.end())
        {
            std::map<std::string, std::string>::const_iterator vit = pit->second.find(key);
            if (vit != pit->second.end())
                return vit->second;
        }
    }
    std::map<std::string, std::string>::const_iterator it = values.find(key);
    if (it != values.end())
        return it->second;
    return std::string();
}

void settings_bi::setProfile(const std::string &exe)
{
    activeProfile = exe;
    if (!exe.empty() && !hasProfile(exe))
        profiles[exe] = std::map<std::string, std::string>();
    log_bi::write("settings: active profile '%s'", exe.empty() ? "(global)" : exe.c_str());
}

bool settings_bi::hasProfile(const std::string &exe) const
{
    return profiles.find(exe) != profiles.end();
}

bool settings_bi::saveProfile(const std::string &exe, resource_usage_bi *ru,
                              overlay_bi *ov, draw_batteryinfo_bi *draw, batteryinfo_bi *bi)
{
    if (exe.empty())
        return false;

    std::string prev = activeProfile;
    activeProfile.clear();

    std::map<std::string, std::string> overrides;

    // Collect current state into a temp map
    settings_bi collector;
    collector.collectFrom(ru, ov, draw, bi);

    // Diff against global values to get only overrides
    for (std::map<std::string, std::string>::const_iterator it = collector.values.begin();
         it != collector.values.end(); ++it)
    {
        std::map<std::string, std::string>::const_iterator gv = values.find(it->first);
        if (gv == values.end() || gv->second != it->second)
            overrides[it->first] = it->second;
    }

    profiles[exe] = overrides;
    activeProfile = prev;

    log_bi::write("settings: saved profile '%s' (%zu overrides)", exe.c_str(), overrides.size());
    return true;
}

bool settings_bi::deleteProfile(const std::string &exe)
{
    std::map<std::string, std::map<std::string, std::string>>::iterator it = profiles.find(exe);
    if (it == profiles.end())
        return false;
    profiles.erase(it);
    if (activeProfile == exe)
        activeProfile.clear();
    log_bi::write("settings: deleted profile '%s'", exe.c_str());
    return true;
}

std::vector<std::string> settings_bi::profileList() const
{
    std::vector<std::string> list;
    for (std::map<std::string, std::map<std::string, std::string>>::const_iterator it = profiles.begin();
         it != profiles.end(); ++it)
    {
        list.push_back(it->first);
    }
    return list;
}

bool settings_bi::getBool(const char *key, bool def) const
{
    std::string v = getValue(key);
    if (v.empty())
        return def;
    return v == "1" || v == "true" || v == "yes";
}

int settings_bi::getInt(const char *key, int def) const
{
    std::string v = getValue(key);
    if (v.empty())
        return def;
    return atoi(v.c_str());
}

float settings_bi::getFloat(const char *key, float def) const
{
    std::string v = getValue(key);
    if (v.empty())
        return def;
    return (float)atof(v.c_str());
}

std::string settings_bi::getString(const char *key, const std::string &def) const
{
    std::string v = getValue(key);
    if (v.empty())
        return def;
    return v;
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
    values[key] = std::to_string(value);
}

void settings_bi::setFloat(const char *key, float value)
{
    values[key] = std::format("{:.4f}", value);
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
        ru->cpuInfo.show_cpuTemp = getBool("cpu.temp", ru->cpuInfo.show_cpuTemp);
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

        ru->gpuInfo.show_gpuTemp = getBool("gpu.temp", ru->gpuInfo.show_gpuTemp);
        ru->gpuInfo.show_gpuName = getBool("gpu.name", ru->gpuInfo.show_gpuName);
        ru->gpuInfo.show_gpuLoad = getBool("gpu.load", ru->gpuInfo.show_gpuLoad);
        ru->gpuInfo.show_vram = getBool("gpu.vram", ru->gpuInfo.show_vram);
        ru->gpuInfo.show_gpuPower = getBool("gpu.power", ru->gpuInfo.show_gpuPower);
        ru->gpuInfo.show_adapters = getBool("gpu.adapters", ru->gpuInfo.show_adapters);

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
        ov->overlayAlpha = getInt("hud.alpha", ov->overlayAlpha);
        ov->autoHideOverlay = getBool("hud.autoHide", ov->autoHideOverlay);
        ov->clickable = getBool("hud.clickable", ov->clickable);

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
        ov->hud.showNetwork = getBool("hud.network", ov->hud.showNetwork);
        ov->hud.showDisk = getBool("hud.disk", ov->hud.showDisk);

        std::string orderStr = getString("hud.metricOrder", "");
        ov->hud.metricOrder.clear();
        if (!orderStr.empty())
        {
            size_t pos = 0;
            while (pos < orderStr.size())
            {
                size_t comma = orderStr.find(',', pos);
                std::string token = (comma == std::string::npos)
                    ? orderStr.substr(pos) : orderStr.substr(pos, comma - pos);
                if (!token.empty())
                {
                    int id = std::stoi(token);
                    if (id >= 0 && id < HUD_M_COUNT)
                        ov->hud.metricOrder.push_back(id);
                }
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
        }

        ov->graphHeightMultiplier = getFloat("hud.graphHeight", ov->graphHeightMultiplier);
    }

    if (draw)
    {
        int loadedTheme = getInt("ui.theme", -1);
        if (loadedTheme >= 0 && loadedTheme < draw_batteryinfo_bi::THEME_COUNT)
            draw->setTheme(loadedTheme);
        else
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
        setBool("cpu.temp", ru->cpuInfo.show_cpuTemp);
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

        setBool("gpu.temp", ru->gpuInfo.show_gpuTemp);
        setBool("gpu.name", ru->gpuInfo.show_gpuName);
        setBool("gpu.load", ru->gpuInfo.show_gpuLoad);
        setBool("gpu.vram", ru->gpuInfo.show_vram);
        setBool("gpu.power", ru->gpuInfo.show_gpuPower);
        setBool("gpu.adapters", ru->gpuInfo.show_adapters);

        setInt("mem.unit", ru->memUnit);

        setBool("app.minimizeToTray", ru->minimize_To_Tray);
        setBool("app.exitOnEsc", ru->exit_on_key_esc);
    }

    if (ov)
    {
        setBool("hud.enabled", ov->show_on_screen_display);
        setInt("hud.margin", ov->margin);
        setInt("hud.scale", ov->getScale());
        setInt("hud.alpha", ov->overlayAlpha);
        setBool("hud.autoHide", ov->autoHideOverlay);
        setBool("hud.clickable", ov->clickable);
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
        setBool("hud.network", ov->hud.showNetwork);
        setBool("hud.disk", ov->hud.showDisk);

        std::string orderStr;
        for (size_t oi = 0; oi < ov->hud.metricOrder.size(); ++oi)
        {
            if (oi > 0) orderStr += ',';
            orderStr += std::to_string(ov->hud.metricOrder[oi]);
        }
        setString("hud.metricOrder", orderStr);
        setFloat("hud.graphHeight", ov->graphHeightMultiplier);
    }

    if (draw)
    {
        setInt("ui.theme", draw->getTheme());
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

bool settings_bi::exportJson(const char *path) const
{
    FILE *f = fopen(path, "w");
    if (!f)
        return false;

    fprintf(f, "{\n");
    bool first = true;
    for (std::map<std::string, std::string>::const_iterator it = values.begin();
         it != values.end(); ++it)
    {
        if (!first)
            fprintf(f, ",\n");
        first = false;

        std::string escaped;
        for (size_t i = 0; i < it->second.size(); ++i)
        {
            char c = it->second[i];
            if (c == '"' || c == '\\')
            {
                escaped += '\\';
                escaped += c;
            }
            else if (c == '\n')
                escaped += "\\n";
            else
                escaped += c;
        }

        fprintf(f, "  \"%s\": \"%s\"", it->first.c_str(), escaped.c_str());
    }
    fprintf(f, "\n}\n");

    fclose(f);
    return true;
}

bool settings_bi::importJson(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return false;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::string content((size_t)len, '\0');
    if (fread(&content[0], 1, (size_t)len, f) != (size_t)len)
    {
        fclose(f);
        return false;
    }
    fclose(f);

    size_t pos = 0;
    while (pos < content.size())
    {
        size_t kstart = content.find('"', pos);
        if (kstart == std::string::npos)
            break;

        size_t kend = content.find('"', kstart + 1);
        if (kend == std::string::npos)
            break;

        std::string key = content.substr(kstart + 1, kend - kstart - 1);

        size_t vstart = content.find('"', kend + 1);
        if (vstart == std::string::npos)
            break;

        size_t vend = vstart + 1;
        while (vend < content.size() && content[vend] != '"')
        {
            if (content[vend] == '\\')
                ++vend;
            ++vend;
        }
        if (vend >= content.size())
            break;

        std::string value;
        for (size_t i = vstart + 1; i < vend; ++i)
        {
            if (content[i] == '\\' && i + 1 < vend)
            {
                if (content[i + 1] == 'n')
                    value += '\n';
                else
                    value += content[i + 1];
                ++i;
            }
            else
            {
                value += content[i];
            }
        }

        values[key] = value;
        pos = vend + 1;
    }

    log_bi::write("settings: imported %u values from %s", (unsigned)values.size(), path);
    return true;
}
