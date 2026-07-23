#include "capture_bi.h"

#include "paths_bi.h"
#include "logger_bi.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>

namespace
{
    const char *INDEX_FILE = "index.csv";

    LONGLONG nowFileTime()
    {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        return ((LONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    }

    std::string sanitize(const std::string &s)
    {
        std::string out;
        for (size_t i = 0; i < s.size() && out.size() < 40; ++i)
        {
            char c = s[i];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '-' || c == '_')
            {
                out += c;
            }
        }
        return out.empty() ? std::string("session") : out;
    }

    std::string timestampLabel()
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}",
                           (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
                           (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond);
    }

    std::string timestampFile()
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        return std::format("{:04}{:02}{:02}-{:02}{:02}{:02}",
                           (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
                           (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond);
    }

    std::string csvEscape(const std::string &s)
    {
        std::string out;
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] == ',' || s[i] == '"' || s[i] == '\n')
                continue;
            out += s[i];
        }
        return out;
    }

    bool nextField(const std::string &line, size_t *pos, std::string *out)
    {
        if (*pos > line.size())
            return false;

        size_t comma = line.find(',', *pos);
        if (comma == std::string::npos)
        {
            *out = line.substr(*pos);
            *pos = line.size() + 1;
            return true;
        }

        *out = line.substr(*pos, comma - *pos);
        *pos = comma + 1;
        return true;
    }
}

capture_bi::capture_bi()
    : recording(false),
      startTime100ns(0),
      lastTime100ns(0),
      fullChargedWh(0.0)
{
}

std::string capture_bi::capturesDir()
{
    const std::string &base = paths_bi::dataDir();
    if (base.empty())
        return std::string();

    std::string dir = base + "\\captures";

    if (CreateDirectoryA(dir.c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS)
        return dir;

    return std::string();
}

void capture_bi::start(const std::string &name)
{
    frames.reset();
    power.clear();

    processName = name.empty() ? std::string("unknown") : name;
    startTime100ns = nowFileTime();
    lastTime100ns = startTime100ns;
    fullChargedWh = 0.0;
    recording = true;

    log_bi::write("capture: started for %s", processName.c_str());
}

void capture_bi::discard()
{
    recording = false;
    frames.reset();
    power.clear();
}

void capture_bi::addFrame(double intervalMs, LONGLONG time100ns)
{
    if (!recording)
        return;

    frames.push(intervalMs, time100ns);
    lastTime100ns = time100ns;
}

void capture_bi::addPowerSample(double packageW, bool packageOk,
                                double batteryPct, bool batteryPctOk,
                                double batteryRateW, bool batteryRateOk,
                                double capacityWh)
{
    if (!recording)
        return;

    power_sample_bi s;
    s.time100ns = nowFileTime();
    s.packageW = packageW;
    s.packageValid = packageOk;
    s.batteryPct = batteryPct;
    s.batteryPctValid = batteryPctOk;
    s.batteryRateW = batteryRateW;
    s.batteryRateValid = batteryRateOk;

    power.push_back(s);

    if (capacityWh > 0.0)
        fullChargedWh = capacityWh;
}

double capture_bi::elapsedSeconds() const
{
    if (!recording || startTime100ns == 0)
        return 0.0;

    LONGLONG span = nowFileTime() - startTime100ns;
    return span > 0 ? (double)span / 10000000.0 : 0.0;
}

void capture_bi::computeSummary(summary_bi *out) const
{
    out->process = processName;
    out->label = timestampLabel();
    out->frames = frames.count();

    LONGLONG span = lastTime100ns - startTime100ns;
    out->seconds = span > 0 ? (double)span / 10000000.0 : 0.0;

    if (frames.count() > 0)
    {
        out->avgFps = frames.averageFps();
        out->minMs = frames.minMs();
        out->maxMs = frames.maxMs();
        out->medianMs = frames.medianMs();
        out->stutters = frames.stutters();

        out->low1Valid = frames.hasEnoughFor(0.01);
        out->low01Valid = frames.hasEnoughFor(0.001);
        out->low1Fps = frames.lowFps(0.01);
        out->low01Fps = frames.lowFps(0.001);
    }

    double packageSum = 0.0;
    size_t packageCount = 0;
    double energy100ns = 0.0;

    double batterySum = 0.0;
    size_t batteryCount = 0;

    bool haveFirstPct = false;

    for (size_t i = 0; i < power.size(); ++i)
    {
        const power_sample_bi &s = power[i];

        if (s.packageValid)
        {
            packageSum += s.packageW;
            ++packageCount;

            if (i > 0)
            {
                LONGLONG dt = s.time100ns - power[i - 1].time100ns;
                if (dt > 0 && dt < 100000000LL)
                    energy100ns += s.packageW * (double)dt;
            }
        }

        if (s.batteryRateValid && s.batteryRateW < 0.0)
        {
            batterySum += -s.batteryRateW;
            ++batteryCount;
        }

        if (s.batteryPctValid)
        {
            if (!haveFirstPct)
            {
                out->batteryStartPct = s.batteryPct;
                haveFirstPct = true;
            }
            out->batteryEndPct = s.batteryPct;
            out->batteryValid = true;
        }
    }

    if (packageCount > 0)
    {
        out->avgPackageW = packageSum / (double)packageCount;
        out->packageValid = true;
    }

    if (energy100ns > 0.0)
    {
        out->energyWh = energy100ns / 10000000.0 / 3600.0;
        out->energyValid = true;

        if (out->energyWh > 0.0 && out->frames > 0)
        {
            out->framesPerWattHour = (double)out->frames / out->energyWh;
            out->efficiencyValid = true;
        }
    }

    if (batteryCount > 0)
    {
        out->batteryDrawW = batterySum / (double)batteryCount;

        if (out->batteryDrawW > 0.5 && fullChargedWh > 0.0)
        {
            out->projectedHours = fullChargedWh / out->batteryDrawW;
            out->projectionValid = true;
        }
    }
}

bool capture_bi::writeFrameCsv(const std::string &path) const
{
    FILE *f = fopen(path.c_str(), "w");
    if (!f)
        return false;

    fprintf(f, "frame,time_s,frame_time_ms,fps\n");

    const std::deque<frame_stats_bi::entry_bi> &list = frames.samples();

    size_t index = 0;
    for (std::deque<frame_stats_bi::entry_bi>::const_iterator it = list.begin();
         it != list.end(); ++it, ++index)
    {
        double t = (double)(it->time100ns - startTime100ns) / 10000000.0;
        double ms = it->intervalMs;

        fprintf(f, "%u,%.4f,%.4f,%.2f\n",
                (unsigned)index, t, ms, ms > 0.0 ? 1000.0 / ms : 0.0);
    }

    if (frames.count() > 0)
    {
        fprintf(f, "\n# Summary Statistics\n");
        fprintf(f, "# metric,value\n");
        fprintf(f, "avg_fps,%.2f\n", frames.averageFps());
        fprintf(f, "avg_frame_ms,%.3f\n", frames.averageMs());
        fprintf(f, "median_ms,%.3f\n", frames.medianMs());
        fprintf(f, "min_ms,%.3f\n", frames.minMs());
        fprintf(f, "max_ms,%.3f\n", frames.maxMs());
        fprintf(f, "stutters,%u\n", frames.stutters());
        fprintf(f, "total_frames,%u\n", (unsigned)frames.count());
        fprintf(f, "p01_ms,%.3f\n", frames.percentileMs(0.01));
        fprintf(f, "p1_ms,%.3f\n", frames.percentileMs(0.1));
        fprintf(f, "p5_ms,%.3f\n", frames.percentileMs(0.05));
        fprintf(f, "p50_ms,%.3f\n", frames.percentileMs(0.50));
        fprintf(f, "p95_ms,%.3f\n", frames.percentileMs(0.95));
        fprintf(f, "p99_ms,%.3f\n", frames.percentileMs(0.99));
        fprintf(f, "low_1_pct_fps,%.2f\n", frames.lowFps(0.01));
        fprintf(f, "low_0_1_pct_fps,%.2f\n", frames.lowFps(0.001));
    }

    fclose(f);
    return true;
}

bool capture_bi::appendIndex(const summary_bi &s) const
{
    std::string dir = capturesDir();
    if (dir.empty())
        return false;

    std::string path = dir + "\\" + INDEX_FILE;

    bool fresh = GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES;

    FILE *f = fopen(path.c_str(), "a");
    if (!f)
        return false;

    if (fresh)
    {
        fprintf(f, "label,process,file,seconds,frames,avg_fps,low1_fps,low01_fps,"
                   "median_ms,min_ms,max_ms,stutters,avg_package_w,energy_wh,"
                   "battery_start_pct,battery_end_pct,battery_draw_w,projected_hours\n");
    }

    fprintf(f, "%s,%s,%s,%.2f,%u,%.2f,%.2f,%.2f,%.3f,%.3f,%.3f,%u,%.2f,%.4f,%.1f,%.1f,%.2f,%.2f\n",
            csvEscape(s.label).c_str(),
            csvEscape(s.process).c_str(),
            csvEscape(s.file).c_str(),
            s.seconds,
            (unsigned)s.frames,
            s.avgFps,
            s.low1Valid ? s.low1Fps : 0.0,
            s.low01Valid ? s.low01Fps : 0.0,
            s.medianMs, s.minMs, s.maxMs,
            s.stutters,
            s.packageValid ? s.avgPackageW : 0.0,
            s.energyValid ? s.energyWh : 0.0,
            s.batteryValid ? s.batteryStartPct : 0.0,
            s.batteryValid ? s.batteryEndPct : 0.0,
            s.batteryDrawW,
            s.projectionValid ? s.projectedHours : 0.0);

    fclose(f);
    return true;
}

bool capture_bi::stop(summary_bi *out)
{
    if (!recording)
        return false;

    recording = false;

    summary_bi summary;
    computeSummary(&summary);

    if (summary.frames < 2)
    {
        log_bi::write("capture: discarded, only %u frames", (unsigned)summary.frames);
        frames.reset();
        power.clear();
        return false;
    }

    std::string dir = capturesDir();
    if (!dir.empty())
    {
        summary.file = timestampFile() + "-" + sanitize(processName) + ".csv";

        std::string full = dir + "\\" + summary.file;

        if (writeFrameCsv(full))
        {
            appendIndex(summary);
            log_bi::write("capture: wrote %u frames to %s",
                          (unsigned)summary.frames, summary.file.c_str());
        }
        else
        {
            summary.file.clear();
            log_bi::write("capture: could not write %s", full.c_str());
        }
    }

    if (out)
        *out = summary;

    frames.reset();
    power.clear();

    return true;
}

bool capture_bi::loadIndex(std::vector<summary_bi> *out)
{
    if (!out)
        return false;

    out->clear();

    std::string dir = capturesDir();
    if (dir.empty())
        return false;

    std::string path = dir + "\\" + INDEX_FILE;

    FILE *f = fopen(path.c_str(), "r");
    if (!f)
        return false;

    char line[1024];
    bool first = true;

    while (fgets(line, sizeof(line), f))
    {
        std::string text(line);

        while (!text.empty() && (text[text.size() - 1] == '\n' || text[text.size() - 1] == '\r'))
            text.erase(text.size() - 1);

        if (text.empty())
            continue;

        if (first)
        {
            first = false;
            if (text.compare(0, 5, "label") == 0)
                continue;
        }

        summary_bi s;
        size_t pos = 0;
        std::string field;

        if (!nextField(text, &pos, &s.label))
            continue;
        if (!nextField(text, &pos, &s.process))
            continue;
        if (!nextField(text, &pos, &s.file))
            continue;

        if (nextField(text, &pos, &field))
            s.seconds = atof(field.c_str());
        if (nextField(text, &pos, &field))
            s.frames = (size_t)atoi(field.c_str());
        if (nextField(text, &pos, &field))
            s.avgFps = atof(field.c_str());
        if (nextField(text, &pos, &field))
        {
            s.low1Fps = atof(field.c_str());
            s.low1Valid = s.low1Fps > 0.0;
        }
        if (nextField(text, &pos, &field))
        {
            s.low01Fps = atof(field.c_str());
            s.low01Valid = s.low01Fps > 0.0;
        }
        if (nextField(text, &pos, &field))
            s.medianMs = atof(field.c_str());
        if (nextField(text, &pos, &field))
            s.minMs = atof(field.c_str());
        if (nextField(text, &pos, &field))
            s.maxMs = atof(field.c_str());
        if (nextField(text, &pos, &field))
            s.stutters = (unsigned)atoi(field.c_str());
        if (nextField(text, &pos, &field))
        {
            s.avgPackageW = atof(field.c_str());
            s.packageValid = s.avgPackageW > 0.0;
        }
        if (nextField(text, &pos, &field))
        {
            s.energyWh = atof(field.c_str());
            s.energyValid = s.energyWh > 0.0;
        }
        if (nextField(text, &pos, &field))
            s.batteryStartPct = atof(field.c_str());
        if (nextField(text, &pos, &field))
        {
            s.batteryEndPct = atof(field.c_str());
            s.batteryValid = s.batteryStartPct > 0.0 || s.batteryEndPct > 0.0;
        }
        if (nextField(text, &pos, &field))
            s.batteryDrawW = atof(field.c_str());
        if (nextField(text, &pos, &field))
        {
            s.projectedHours = atof(field.c_str());
            s.projectionValid = s.projectedHours > 0.0;
        }

        out->push_back(s);
    }

    fclose(f);

    if (out->size() > 1)
    {
        for (size_t i = 0; i < out->size() / 2; ++i)
        {
            summary_bi tmp = (*out)[i];
            (*out)[i] = (*out)[out->size() - 1 - i];
            (*out)[out->size() - 1 - i] = tmp;
        }
    }

    return true;
}
