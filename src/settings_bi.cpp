#include "settings_bi.h"

#include "resource_usage_bi.h"
#include "overlay_bi.h"
#include "draw_batteryinfo_bi.h"
#include "BatteryInfo.h"
#include "paths_bi.h"
#include "logger_bi.h"
#include "app_identity_bi.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <format>
#include <limits>
#include <system_error>
#include <io.h>

namespace
{
    const char *PROFILE_JSON_PREFIX = "profile:";
    const size_t MAX_JSON_BYTES = 4 * 1024 * 1024;
    const size_t MAX_JSON_ENTRIES = 10000;
    const size_t MAX_JSON_KEY_BYTES = 4096;
    const size_t MAX_JSON_VALUE_BYTES = 1024 * 1024;

    // MUST have exactly HUD_M_COUNT entries — a missing key is a nullptr that
    // crashes applyTo/collectFrom via strlen(nullptr). Keep in sync with the
    // HUD_M_* enum in hud_bi.h.
    const char *METRIC_KEYS[HUD_M_COUNT] = {
        "fps", "pre", "gpums", "cpu", "gpu", "ram", "commit", "cpuw", "gpuw", "batteryd",
        "netdown", "netup", "disk", "cputemp", "gputemp", "battemp", "gpufan"};
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

    bool isIniSafeValue(const std::string &value)
    {
        return value.find('\0') == std::string::npos &&
               value.find('\r') == std::string::npos &&
               value.find('\n') == std::string::npos &&
               (value.empty() ||
                (!std::isspace((unsigned char)value.front()) &&
                 !std::isspace((unsigned char)value.back())));
    }

    bool isIniSafeKey(const std::string &key)
    {
        return !key.empty() && isIniSafeValue(key) &&
               key.front() != '#' && key.front() != ';' && key.front() != '[' &&
               key.find('=') == std::string::npos;
    }

    bool isIniSafeProfile(const std::string &profile)
    {
        return !profile.empty() &&
               profile.find('\0') == std::string::npos &&
               profile.find('\r') == std::string::npos &&
               profile.find('\n') == std::string::npos &&
               profile.find(':') == std::string::npos;
    }

    std::string hexEncode(const std::string &value)
    {
        static const char HEX[] = "0123456789abcdef";
        std::string encoded;
        encoded.reserve(value.size() * 2);
        for (unsigned char c : value)
        {
            encoded.push_back(HEX[c >> 4]);
            encoded.push_back(HEX[c & 0x0f]);
        }
        return encoded;
    }

    bool hexDecode(const std::string &encoded, std::string *value)
    {
        if (encoded.empty() || (encoded.size() % 2) != 0)
            return false;

        value->clear();
        value->reserve(encoded.size() / 2);
        for (size_t i = 0; i < encoded.size(); i += 2)
        {
            const auto nibble = [](char c) -> int {
                if (c >= '0' && c <= '9')
                    return c - '0';
                if (c >= 'a' && c <= 'f')
                    return c - 'a' + 10;
                if (c >= 'A' && c <= 'F')
                    return c - 'A' + 10;
                return -1;
            };
            int high = nibble(encoded[i]);
            int low = nibble(encoded[i + 1]);
            if (high < 0 || low < 0)
                return false;
            value->push_back((char)((high << 4) | low));
        }
        return true;
    }

    void skipJsonWhitespace(const std::string &text, size_t *pos)
    {
        while (*pos < text.size() &&
               std::isspace((unsigned char)text[*pos]) != 0)
        {
            ++*pos;
        }
    }

    bool parseHex4(const std::string &text, size_t pos, unsigned *value)
    {
        if (pos + 4 > text.size())
            return false;

        unsigned result = 0;
        for (size_t i = 0; i < 4; ++i)
        {
            unsigned digit = 0;
            char c = text[pos + i];
            if (c >= '0' && c <= '9')
                digit = (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f')
                digit = (unsigned)(c - 'a') + 10;
            else if (c >= 'A' && c <= 'F')
                digit = (unsigned)(c - 'A') + 10;
            else
                return false;
            result = (result << 4) | digit;
        }

        *value = result;
        return true;
    }

    bool appendUtf8(std::string *out, unsigned codePoint)
    {
        if (codePoint <= 0x7f)
        {
            out->push_back((char)codePoint);
        }
        else if (codePoint <= 0x7ff)
        {
            out->push_back((char)(0xc0 | (codePoint >> 6)));
            out->push_back((char)(0x80 | (codePoint & 0x3f)));
        }
        else if (codePoint <= 0xffff)
        {
            if (codePoint >= 0xd800 && codePoint <= 0xdfff)
                return false;
            out->push_back((char)(0xe0 | (codePoint >> 12)));
            out->push_back((char)(0x80 | ((codePoint >> 6) & 0x3f)));
            out->push_back((char)(0x80 | (codePoint & 0x3f)));
        }
        else if (codePoint <= 0x10ffff)
        {
            out->push_back((char)(0xf0 | (codePoint >> 18)));
            out->push_back((char)(0x80 | ((codePoint >> 12) & 0x3f)));
            out->push_back((char)(0x80 | ((codePoint >> 6) & 0x3f)));
            out->push_back((char)(0x80 | (codePoint & 0x3f)));
        }
        else
        {
            return false;
        }
        return true;
    }

    bool parseJsonString(const std::string &text, size_t *pos,
                         size_t maxBytes, std::string *out)
    {
        if (*pos >= text.size() || text[*pos] != '"')
            return false;

        ++*pos;
        out->clear();

        while (*pos < text.size())
        {
            unsigned char c = (unsigned char)text[(*pos)++];
            if (c == '"')
                return true;
            if (c < 0x20)
                return false;

            if (c != '\\')
            {
                out->push_back((char)c);
            }
            else
            {
                if (*pos >= text.size())
                    return false;

                char escaped = text[(*pos)++];
                switch (escaped)
                {
                case '"': out->push_back('"'); break;
                case '\\': out->push_back('\\'); break;
                case '/': out->push_back('/'); break;
                case 'b': out->push_back('\b'); break;
                case 'f': out->push_back('\f'); break;
                case 'n': out->push_back('\n'); break;
                case 'r': out->push_back('\r'); break;
                case 't': out->push_back('\t'); break;
                case 'u':
                {
                    unsigned first = 0;
                    if (!parseHex4(text, *pos, &first))
                        return false;
                    *pos += 4;

                    unsigned codePoint = first;
                    if (first >= 0xd800 && first <= 0xdbff)
                    {
                        if (*pos + 6 > text.size() ||
                            text[*pos] != '\\' || text[*pos + 1] != 'u')
                        {
                            return false;
                        }

                        unsigned second = 0;
                        if (!parseHex4(text, *pos + 2, &second) ||
                            second < 0xdc00 || second > 0xdfff)
                        {
                            return false;
                        }

                        *pos += 6;
                        codePoint = 0x10000 +
                            ((first - 0xd800) << 10) + (second - 0xdc00);
                    }
                    else if (first >= 0xdc00 && first <= 0xdfff)
                    {
                        return false;
                    }

                    if (!appendUtf8(out, codePoint))
                        return false;
                    break;
                }
                default:
                    return false;
                }
            }

            if (out->size() > maxBytes)
                return false;
        }

        return false;
    }

    bool parseFlatJsonObject(const std::string &text,
                             std::map<std::string, std::string> *out)
    {
        size_t pos = 0;
        skipJsonWhitespace(text, &pos);
        if (pos >= text.size() || text[pos++] != '{')
            return false;

        skipJsonWhitespace(text, &pos);
        if (pos < text.size() && text[pos] == '}')
        {
            ++pos;
            skipJsonWhitespace(text, &pos);
            return pos == text.size();
        }

        while (pos < text.size())
        {
            if (out->size() >= MAX_JSON_ENTRIES)
                return false;

            std::string key;
            std::string value;
            if (!parseJsonString(text, &pos, MAX_JSON_KEY_BYTES, &key) ||
                key.empty())
            {
                return false;
            }

            skipJsonWhitespace(text, &pos);
            if (pos >= text.size() || text[pos++] != ':')
                return false;

            skipJsonWhitespace(text, &pos);
            if (!parseJsonString(text, &pos, MAX_JSON_VALUE_BYTES, &value))
                return false;

            if (!out->emplace(key, value).second)
                return false;

            skipJsonWhitespace(text, &pos);
            if (pos >= text.size())
                return false;
            if (text[pos] == '}')
            {
                ++pos;
                skipJsonWhitespace(text, &pos);
                return pos == text.size();
            }
            if (text[pos++] != ',')
                return false;
            skipJsonWhitespace(text, &pos);
        }

        return false;
    }

    void writeJsonString(FILE *f, const std::string &text)
    {
        static const char HEX[] = "0123456789abcdef";

        fputc('"', f);
        for (size_t i = 0; i < text.size(); ++i)
        {
            unsigned char c = (unsigned char)text[i];
            switch (c)
            {
            case '"': fputs("\\\"", f); break;
            case '\\': fputs("\\\\", f); break;
            case '\b': fputs("\\b", f); break;
            case '\f': fputs("\\f", f); break;
            case '\n': fputs("\\n", f); break;
            case '\r': fputs("\\r", f); break;
            case '\t': fputs("\\t", f); break;
            default:
                if (c < 0x20)
                {
                    char escaped[7] = {
                        '\\', 'u', '0', '0', HEX[c >> 4], HEX[c & 0xf], '\0'};
                    fputs(escaped, f);
                }
                else
                {
                    fputc(c, f);
                }
                break;
            }
        }
        fputc('"', f);
    }

    FILE *createExclusiveRegularFile(const std::wstring &path)
    {
        HANDLE handle = CreateFileW(
            path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_TEMPORARY, NULL);
        if (handle == INVALID_HANDLE_VALUE)
            return NULL;

        int fd = _open_osfhandle(
            (intptr_t)handle, _O_WRONLY | _O_BINARY);
        if (fd < 0)
        {
            CloseHandle(handle);
            return NULL;
        }

        FILE *file = _fdopen(fd, "wb");
        if (!file)
            _close(fd);
        return file;
    }
}

Result<void> settings_bi::load()
{
    values.clear();
    profiles.clear();
    activeProfile.clear();

    std::wstring path = paths_bi::inDataDirWide(L"settings.ini");
    if (path.empty())
        return Result<void>(std::string("settings: data dir not available"));

    FILE *f = _wfopen(path.c_str(), L"rb");
    if (!f)
    {
        log_bi::write("settings: no saved file yet, using defaults");
        return Result<void>();
    }

    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return Result<void>(std::string("settings: cannot read the settings file"));
    }
    long fileLength = ftell(f);
    if (fileLength < 0 || (size_t)fileLength > MAX_JSON_BYTES ||
        fseek(f, 0, SEEK_SET) != 0)
    {
        fclose(f);
        return Result<void>(std::string("settings: settings file is too large"));
    }

    std::string content((size_t)fileLength, '\0');
    if (fileLength > 0 &&
        fread(&content[0], 1, (size_t)fileLength, f) != (size_t)fileLength)
    {
        fclose(f);
        return Result<void>(std::string("settings: cannot read the settings file"));
    }
    fclose(f);

    bool inNamedSection = false;
    bool inProfileSection = false;
    std::string profileName;
    int lineNum = 0;
    size_t lineStart = 0;
    while (lineStart < content.size())
    {
        ++lineNum;
        size_t lineEnd = content.find('\n', lineStart);
        std::string s = lineEnd == std::string::npos
            ? content.substr(lineStart)
            : content.substr(lineStart, lineEnd - lineStart);
        lineStart = lineEnd == std::string::npos
            ? content.size()
            : lineEnd + 1;

        trim(s);
        if (s.empty() || s[0] == '#' || s[0] == ';')
            continue;
        if (s.find('\0') != std::string::npos)
        {
            log_bi::write("settings: ignoring line %d (embedded NUL)", lineNum);
            continue;
        }

        if (s[0] == '[')
        {
            inNamedSection = true;
            inProfileSection = false;
            profileName.clear();

            size_t close = s.find(']');
            if (close == std::string::npos || close + 1 != s.size())
                continue;

            std::string section = s.substr(1, close - 1);
            if (section.compare(0, 11, "ProfileHex:") == 0)
            {
                inProfileSection =
                    hexDecode(section.substr(11), &profileName) &&
                    isIniSafeProfile(profileName);
            }
            else if (section.compare(0, 8, "Profile:") == 0)
            {
                profileName = section.substr(8);
                inProfileSection = isIniSafeProfile(profileName);
            }
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

        if (inProfileSection)
        {
            profiles[profileName][key] = value;
        }
        else if (!inNamedSection)
        {
            values[key] = value;
        }
        else
        {
            log_bi::write("settings: ignoring line %d in unknown section", lineNum);
        }
    }

    log_bi::write("settings: loaded %u global values, %zu profiles",
                  (unsigned)values.size(), profiles.size());
    return Result<void>();
}

Result<void> settings_bi::save() const
{
    std::wstring path = paths_bi::inDataDirWide(L"settings.ini");
    if (path.empty())
        return Result<void>(std::string("settings: data dir not available"));

    std::wstring temporaryPath = std::format(
        L"{}.tmp.{}.{}", path, (unsigned long)GetCurrentProcessId(),
        (unsigned long long)GetTickCount64());
    FILE *f = createExclusiveRegularFile(temporaryPath);
    if (!f)
    {
        std::string err = "settings: cannot open the settings file for writing";
        log_bi::write("%s", err.c_str());
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
        if (pit->second.empty())
            continue;
        std::string encodedProfile = hexEncode(pit->first);
        fprintf(f, "\n[ProfileHex:%s]\n", encodedProfile.c_str());
        for (std::map<std::string, std::string>::const_iterator it = pit->second.begin();
             it != pit->second.end(); ++it)
        {
            fprintf(f, "%s=%s\n", it->first.c_str(), it->second.c_str());
        }
    }

    bool writeOk = !ferror(f);
    if (writeOk && fflush(f) != 0)
        writeOk = false;
    if (writeOk && _commit(_fileno(f)) != 0)
        writeOk = false;
    if (fclose(f) != 0)
        writeOk = false;

    if (!writeOk)
    {
        DeleteFileW(temporaryPath.c_str());
        return Result<void>(std::string("settings: writing the settings file failed"));
    }

    if (!MoveFileExW(temporaryPath.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DWORD error = GetLastError();
        DeleteFileW(temporaryPath.c_str());
        log_bi::writeErr(error, "settings: replacing the settings file failed");
        return Result<void>(std::string("settings: replacing the settings file failed"));
    }

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

    std::map<std::string, std::string> overrides;

    settings_bi collector;
    collector.collectStateFrom(ru, ov, draw, bi);

    for (std::map<std::string, std::string>::const_iterator it = collector.values.begin();
         it != collector.values.end(); ++it)
    {
        std::map<std::string, std::string>::const_iterator gv = values.find(it->first);
        if (gv == values.end() || gv->second != it->second)
            overrides[it->first] = it->second;
    }

    if (overrides.empty())
        profiles.erase(exe);
    else
        profiles[exe] = overrides;

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
    if (v == "1" || v == "true" || v == "yes")
        return true;
    if (v == "0" || v == "false" || v == "no")
        return false;
    return def;
}

int settings_bi::getInt(const char *key, int def) const
{
    std::string v = getValue(key);
    if (v.empty())
        return def;

    int parsed = 0;
    std::from_chars_result result =
        std::from_chars(v.data(), v.data() + v.size(), parsed);
    if (result.ec != std::errc() || result.ptr != v.data() + v.size())
        return def;
    return parsed;
}

float settings_bi::getFloat(const char *key, float def) const
{
    std::string v = getValue(key);
    if (v.empty())
        return def;

    errno = 0;
    char *end = NULL;
    float parsed = strtof(v.c_str(), &end);
    if (errno == ERANGE || end != v.c_str() + v.size() ||
        !std::isfinite(parsed))
    {
        return def;
    }
    return parsed;
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
        ru->gpuInfo.show_gpuFan = getBool("gpu.fan", ru->gpuInfo.show_gpuFan);
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
        int margin = getInt("hud.margin", ov->margin);
        ov->margin = std::clamp(margin, 0, 200);
        ov->setScale(getInt("hud.scale", ov->getScale()));
        int alpha = getInt("hud.alpha", ov->overlayAlpha);
        ov->overlayAlpha = std::clamp(alpha, 0, 255);
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

        std::vector<int> parsedOrder;
        if (!hud_parseMetricOrder(getString("hud.metricOrder", ""), parsedOrder))
            log_bi::write("settings: ignoring invalid hud.metricOrder");
        ov->hud.metricOrder.swap(parsedOrder);

        float graphHeight =
            getFloat("hud.graphHeight", ov->graphHeightMultiplier);
        ov->graphHeightMultiplier = std::clamp(graphHeight, 0.5f, 3.0f);
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
        accent.r = std::clamp(getFloat("ui.accent.r", cur.r), 0.0f, 1.0f);
        accent.g = std::clamp(getFloat("ui.accent.g", cur.g), 0.0f, 1.0f);
        accent.b = std::clamp(getFloat("ui.accent.b", cur.b), 0.0f, 1.0f);
        accent.a = 1.0f;
        draw->setAccentColor(accent);
    }
}

void settings_bi::collectFrom(const resource_usage_bi *ru, const overlay_bi *ov,
                              const draw_batteryinfo_bi *draw, const batteryinfo_bi *bi)
{
    settings_bi collector;
    collector.collectStateFrom(ru, ov, draw, bi);

    const std::map<std::string, std::string> *overrides = NULL;
    if (!activeProfile.empty())
    {
        std::map<std::string, std::map<std::string, std::string>>::const_iterator it =
            profiles.find(activeProfile);
        if (it != profiles.end())
            overrides = &it->second;
    }

    for (std::map<std::string, std::string>::const_iterator it = collector.values.begin();
         it != collector.values.end(); ++it)
    {
        if (!overrides || overrides->find(it->first) == overrides->end())
            values[it->first] = it->second;
    }
}

void settings_bi::collectStateFrom(const resource_usage_bi *ru, const overlay_bi *ov,
                                   const draw_batteryinfo_bi *draw,
                                   const batteryinfo_bi *bi)
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
        setBool("gpu.fan", ru->gpuInfo.show_gpuFan);
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

bool settings_bi::exportJson(const wchar_t *path) const
{
    if (!path || !*path)
        return false;

    FILE *f = _wfopen(path, L"wb");
    if (!f)
        return false;

    fprintf(f, "{\n");
    bool first = true;
    const auto writeEntry = [&](const std::string &key, const std::string &value) {
        if (!first)
            fprintf(f, ",\n");
        first = false;
        fputs("  ", f);
        writeJsonString(f, key);
        fputs(": ", f);
        writeJsonString(f, value);
    };

    for (std::map<std::string, std::string>::const_iterator it = values.begin();
         it != values.end(); ++it)
        writeEntry(it->first, it->second);

    for (std::map<std::string, std::map<std::string, std::string>>::const_iterator pit =
             profiles.begin();
         pit != profiles.end(); ++pit)
    {
        for (std::map<std::string, std::string>::const_iterator it = pit->second.begin();
             it != pit->second.end(); ++it)
        {
            writeEntry(std::string(PROFILE_JSON_PREFIX) + pit->first + ":" + it->first,
                       it->second);
        }
    }
    fprintf(f, "\n}\n");

    bool ok = !ferror(f);
    if (fclose(f) != 0)
        ok = false;
    return ok;
}

bool settings_bi::importJson(const wchar_t *path)
{
    if (!path || !*path)
        return false;

    FILE *f = _wfopen(path, L"rb");
    if (!f)
        return false;

    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return false;
    }
    long len = ftell(f);
    if (len < 0 || (size_t)len > MAX_JSON_BYTES ||
        fseek(f, 0, SEEK_SET) != 0)
    {
        fclose(f);
        return false;
    }

    std::string content((size_t)len, '\0');
    if (len > 0 && fread(&content[0], 1, (size_t)len, f) != (size_t)len)
    {
        fclose(f);
        return false;
    }
    fclose(f);

    std::map<std::string, std::string> flat;
    if (!parseFlatJsonObject(content, &flat))
    {
        log_bi::write("settings: rejected malformed JSON import");
        return false;
    }

    std::map<std::string, std::string> importedValues;
    std::map<std::string, std::map<std::string, std::string>> importedProfiles;
    const size_t prefixLen = strlen(PROFILE_JSON_PREFIX);

    for (std::map<std::string, std::string>::const_iterator it = flat.begin();
         it != flat.end(); ++it)
    {
        if (it->first.compare(0, prefixLen, PROFILE_JSON_PREFIX) != 0)
        {
            if (!isIniSafeKey(it->first) || !isIniSafeValue(it->second))
            {
                log_bi::write("settings: rejected unsafe key or value in JSON import");
                return false;
            }
            importedValues[it->first] = it->second;
            continue;
        }

        size_t separator = it->first.find(':', prefixLen);
        if (separator == std::string::npos ||
            separator == prefixLen || separator + 1 >= it->first.size())
        {
            log_bi::write("settings: rejected malformed profile key in JSON import");
            return false;
        }

        std::string profile = it->first.substr(prefixLen, separator - prefixLen);
        std::string key = it->first.substr(separator + 1);
        if (!isIniSafeProfile(profile) || !isIniSafeKey(key) ||
            !isIniSafeValue(it->second))
        {
            log_bi::write("settings: rejected unsafe profile data in JSON import");
            return false;
        }
        importedProfiles[profile][key] = it->second;
    }

    values.swap(importedValues);
    profiles.swap(importedProfiles);
    activeProfile.clear();

    log_bi::write("settings: imported %u global values, %zu profiles",
                  (unsigned)values.size(), profiles.size());
    return true;
}
