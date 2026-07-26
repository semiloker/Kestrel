#include "capture_bi.h"

#include "csv_bi.h"
#include "paths_bi.h"
#include "logger_bi.h"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cstdint>
#include <deque>
#include <fcntl.h>
#include <format>
#include <io.h>
#include <limits>

namespace
{
    const LONGLONG MAX_INDEX_BYTES = 8LL * 1024LL * 1024LL;
    const size_t MAX_HISTORY_ROWS = 1000;

    LONGLONG nowFileTime()
    {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        return ((LONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    }

    std::string sanitize(const std::string &s)
    {
        std::wstring input = paths_bi::utf8ToWide(s);
        std::wstring out;
        for (size_t i = 0; i < input.size() && out.size() < 40; ++i)
        {
            wchar_t c = input[i];
            if (c >= 0xd800 && c <= 0xdbff)
            {
                if (i + 1 >= input.size() || input[i + 1] < 0xdc00 ||
                    input[i + 1] > 0xdfff || out.size() + 2 > 40)
                {
                    continue;
                }
                out += c;
                out += input[++i];
                continue;
            }
            if (c >= 0xdc00 && c <= 0xdfff)
                continue;
            if (c >= 0x20 && wcschr(L"<>:\"/\\|?*", c) == NULL)
                out += c;
        }
        while (!out.empty() && (out.back() == L'.' || out.back() == L' '))
            out.pop_back();
        std::string utf8 = paths_bi::wideToUtf8(out);
        return utf8.empty() ? std::string("session") : utf8;
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
        return std::format("{:04}{:02}{:02}-{:02}{:02}{:02}-{:03}",
                           (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
                           (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond,
                           (unsigned)st.wMilliseconds);
    }

    bool parseNumber(const std::string &text, double *value)
    {
        if (text.empty())
            return false;

        errno = 0;
        char *end = NULL;
        double parsed = strtod(text.c_str(), &end);
        if (errno == ERANGE || end != text.c_str() + text.size() ||
            !std::isfinite(parsed))
        {
            return false;
        }
        *value = parsed;
        return true;
    }

    bool parseCount(const std::string &text, size_t *value)
    {
        unsigned long long parsed = 0;
        std::from_chars_result result =
            std::from_chars(text.data(), text.data() + text.size(), parsed);
        if (text.empty() || result.ec != std::errc() ||
            result.ptr != text.data() + text.size() ||
            parsed > (std::numeric_limits<size_t>::max)())
        {
            return false;
        }
        *value = (size_t)parsed;
        return true;
    }

    FILE *openRegularAppend(const std::wstring &path, bool *emptyOut)
    {
        if (emptyOut)
            *emptyOut = false;

        HANDLE handle = CreateFileW(
            path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
        if (handle == INVALID_HANDLE_VALUE)
            return NULL;

        FILE_ATTRIBUTE_TAG_INFO attributes = {};
        BY_HANDLE_FILE_INFORMATION information = {};
        if (GetFileType(handle) != FILE_TYPE_DISK ||
            !GetFileInformationByHandleEx(
                handle, FileAttributeTagInfo, &attributes, sizeof(attributes)) ||
            (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
            !GetFileInformationByHandle(handle, &information) ||
            information.nNumberOfLinks != 1)
        {
            CloseHandle(handle);
            return NULL;
        }

        if (emptyOut)
            *emptyOut = information.nFileSizeHigh == 0 &&
                        information.nFileSizeLow == 0;

        LARGE_INTEGER offset = {};
        if (!SetFilePointerEx(handle, offset, NULL, FILE_END))
        {
            CloseHandle(handle);
            return NULL;
        }

        int fd = _open_osfhandle(
            (intptr_t)handle, _O_WRONLY | _O_BINARY | _O_APPEND);
        if (fd < 0)
        {
            CloseHandle(handle);
            return NULL;
        }

        FILE *file = _fdopen(fd, "ab");
        if (!file)
            _close(fd);
        return file;
    }

    FILE *openRegularRead(const std::wstring &path)
    {
        HANDLE handle = CreateFileW(
            path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT |
                FILE_FLAG_SEQUENTIAL_SCAN,
            NULL);
        if (handle == INVALID_HANDLE_VALUE)
            return NULL;

        FILE_ATTRIBUTE_TAG_INFO attributes = {};
        BY_HANDLE_FILE_INFORMATION information = {};
        LARGE_INTEGER size = {};
        if (GetFileType(handle) != FILE_TYPE_DISK ||
            !GetFileInformationByHandleEx(
                handle, FileAttributeTagInfo, &attributes, sizeof(attributes)) ||
            (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
            !GetFileInformationByHandle(handle, &information) ||
            information.nNumberOfLinks != 1 ||
            !GetFileSizeEx(handle, &size) ||
            size.QuadPart < 0 || size.QuadPart > MAX_INDEX_BYTES)
        {
            CloseHandle(handle);
            return NULL;
        }

        int fd = _open_osfhandle((intptr_t)handle, _O_RDONLY | _O_BINARY);
        if (fd < 0)
        {
            CloseHandle(handle);
            return NULL;
        }

        FILE *file = _fdopen(fd, "rb");
        if (!file)
            _close(fd);
        return file;
    }

}

capture_bi::capture_bi()
    : recording(false),
      targetPid(0),
      startTime100ns(0),
      lastTime100ns(0),
      fullChargedWh(0.0)
{
    frames.setCapacity(MAX_CAPTURE_FRAMES);
}

std::wstring capture_bi::capturesDir()
{
    const std::wstring &base = paths_bi::dataDirWide();
    if (base.empty())
        return std::wstring();

    std::wstring dir = base + L"\\captures";

    if (CreateDirectoryW(dir.c_str(), NULL))
        return dir;
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        DWORD attributes = GetFileAttributesW(dir.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
        {
            return dir;
        }
    }

    return std::wstring();
}

void capture_bi::start(const std::string &name, DWORD processId)
{
    frames.reset();
    power.clear();

    processName = name.empty() ? std::string("unknown") : name;
    startLabel = timestampLabel();
    targetPid = processId;
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

bool capture_bi::addFrame(double intervalMs, LONGLONG time100ns)
{
    if (!recording)
        return true;
    if (time100ns < startTime100ns)
        return true;
    if (frames.count() >= MAX_CAPTURE_FRAMES)
        return false;

    size_t before = frames.count();
    frames.push(intervalMs, time100ns);
    if (frames.count() > before)
        lastTime100ns = time100ns;
    return true;
}

bool capture_bi::addPowerSample(double packageW, bool packageOk,
                                double batteryPct, bool batteryPctOk,
                                double batteryRateW, bool batteryRateOk,
                                double capacityWh)
{
    if (!recording)
        return true;

    power_sample_bi s;
    s.time100ns = nowFileTime();
    if (!power.empty())
    {
        LONGLONG elapsed = s.time100ns - power.back().time100ns;
        if (elapsed < 10000000LL)
            return true;
    }
    if (power.size() >= MAX_POWER_SAMPLES)
        return false;

    s.packageW = packageW;
    s.packageValid = packageOk;
    s.batteryPct = batteryPct;
    s.batteryPctValid = batteryPctOk;
    s.batteryRateW = batteryRateW;
    s.batteryRateValid = batteryRateOk;

    power.push_back(s);

    if (capacityWh > 0.0)
        fullChargedWh = capacityWh;
    return true;
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
    out->label = startLabel;
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
        if (s.time100ns > lastTime100ns)
            continue;

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

bool capture_bi::writeFrameCsv(const std::wstring &path, DWORD *errorOut) const
{
    if (errorOut)
        *errorOut = ERROR_SUCCESS;

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        if (errorOut)
            *errorOut = GetLastError();
        return false;
    }

    int fd = _open_osfhandle((intptr_t)file, _O_WRONLY | _O_TEXT);
    if (fd < 0)
    {
        CloseHandle(file);
        if (errorOut)
            *errorOut = ERROR_OPEN_FAILED;
        return false;
    }

    FILE *f = _fdopen(fd, "w");
    if (!f)
    {
        _close(fd);
        if (errorOut)
            *errorOut = ERROR_OPEN_FAILED;
        return false;
    }

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

    bool ok = !ferror(f);
    if (fclose(f) != 0)
        ok = false;
    if (!ok)
    {
        DeleteFileW(path.c_str());
        if (errorOut)
            *errorOut = ERROR_WRITE_FAULT;
    }
    return ok;
}

bool capture_bi::appendIndex(const summary_bi &s) const
{
    std::wstring dir = capturesDir();
    if (dir.empty())
        return false;

    std::wstring path = dir + L"\\index.csv";

    bool fresh = false;
    FILE *f = openRegularAppend(path, &fresh);
    if (!f)
        return false;

    if (fresh)
    {
        fprintf(f, "label,process,file,seconds,frames,avg_fps,low1_fps,low01_fps,"
                   "median_ms,min_ms,max_ms,stutters,avg_package_w,energy_wh,"
                   "battery_start_pct,battery_end_pct,battery_draw_w,projected_hours\n");
    }

    fprintf(f, "%s,%s,%s,%.2f,%u,%.2f,%.2f,%.2f,%.3f,%.3f,%.3f,%u,%.2f,%.4f,%.1f,%.1f,%.2f,%.2f\n",
            csv_bi::escape(s.label).c_str(),
            csv_bi::escape(s.process).c_str(),
            csv_bi::escape(s.file).c_str(),
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

    bool ok = !ferror(f);
    if (ok && fflush(f) != 0)
        ok = false;
    if (ok && _commit(_fileno(f)) != 0)
        ok = false;
    if (fclose(f) != 0)
        ok = false;
    return ok;
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

    std::wstring dir = capturesDir();
    if (!dir.empty())
    {
        std::string base = timestampFile() + "-" + sanitize(processName);
        std::wstring full;
        DWORD writeError = ERROR_FILE_EXISTS;

        for (unsigned attempt = 0; attempt < 1000; ++attempt)
        {
            summary.file = base;
            if (attempt > 0)
                summary.file += "-" + std::to_string(attempt);
            summary.file += ".csv";
            std::wstring wideFile = paths_bi::utf8ToWide(summary.file);
            if (wideFile.empty())
            {
                writeError = ERROR_INVALID_NAME;
                break;
            }
            full = dir + L"\\" + wideFile;

            if (writeFrameCsv(full, &writeError))
                break;
            if (writeError != ERROR_FILE_EXISTS &&
                writeError != ERROR_ALREADY_EXISTS)
            {
                break;
            }
        }

        if (writeError == ERROR_SUCCESS)
        {
            if (!appendIndex(summary))
                log_bi::write("capture: frame CSV written, but index update failed");
            log_bi::write("capture: wrote %u frames to %s",
                          (unsigned)summary.frames, summary.file.c_str());
        }
        else
        {
            summary.file.clear();
            log_bi::write("capture: could not create a unique output file");
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

    std::wstring dir = capturesDir();
    if (dir.empty())
        return false;

    std::wstring path = dir + L"\\index.csv";

    FILE *f = openRegularRead(path);
    if (!f)
        return false;

    char line[4096];
    bool first = true;
    std::deque<summary_bi> history;

    while (fgets(line, sizeof(line), f))
    {
        if (!strchr(line, '\n') && !feof(f))
        {
            int c = 0;
            while ((c = fgetc(f)) != EOF && c != '\n')
            {
            }
            continue;
        }

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

        std::string fields[18];
        size_t pos = 0;
        bool valid = true;
        for (std::string &field : fields)
        {
            if (!csv_bi::nextField(text, &pos, &field))
            {
                valid = false;
                break;
            }
        }
        if (!valid || pos <= text.size())
            continue;

        summary_bi s;
        s.label = fields[0];
        s.process = fields[1];
        s.file = fields[2];

        size_t stutterCount = 0;
        if (!parseNumber(fields[3], &s.seconds) ||
            !parseCount(fields[4], &s.frames) ||
            !parseNumber(fields[5], &s.avgFps) ||
            !parseNumber(fields[6], &s.low1Fps) ||
            !parseNumber(fields[7], &s.low01Fps) ||
            !parseNumber(fields[8], &s.medianMs) ||
            !parseNumber(fields[9], &s.minMs) ||
            !parseNumber(fields[10], &s.maxMs) ||
            !parseCount(fields[11], &stutterCount) ||
            stutterCount > (std::numeric_limits<unsigned>::max)() ||
            !parseNumber(fields[12], &s.avgPackageW) ||
            !parseNumber(fields[13], &s.energyWh) ||
            !parseNumber(fields[14], &s.batteryStartPct) ||
            !parseNumber(fields[15], &s.batteryEndPct) ||
            !parseNumber(fields[16], &s.batteryDrawW) ||
            !parseNumber(fields[17], &s.projectedHours))
        {
            continue;
        }

        s.stutters = (unsigned)stutterCount;
        s.low1Valid = s.low1Fps > 0.0;
        s.low01Valid = s.low01Fps > 0.0;
        s.packageValid = s.avgPackageW > 0.0;
        s.energyValid = s.energyWh > 0.0;
        s.batteryValid = s.batteryStartPct > 0.0 || s.batteryEndPct > 0.0;
        s.projectionValid = s.projectedHours > 0.0;

        history.push_back(s);
        if (history.size() > MAX_HISTORY_ROWS)
            history.pop_front();
    }

    fclose(f);

    out->reserve(history.size());
    for (std::deque<summary_bi>::const_reverse_iterator it = history.rbegin();
         it != history.rend(); ++it)
        out->push_back(*it);

    return true;
}
