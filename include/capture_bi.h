#ifndef CAPTURE_BI_H
#define CAPTURE_BI_H

#include <windows.h>
#include <string>
#include <vector>

#include "frame_stats_bi.h"

class capture_bi
{
public:
    static constexpr size_t MAX_CAPTURE_FRAMES = 240000;
    static constexpr size_t MAX_POWER_SAMPLES = 21600;

    struct summary_bi
    {
        std::string label;
        std::string process;
        std::string file;

        double seconds = 0.0;
        size_t frames = 0;

        double avgFps = 0.0;
        double low1Fps = 0.0;
        double low01Fps = 0.0;
        bool low1Valid = false;
        bool low01Valid = false;

        double minMs = 0.0;
        double maxMs = 0.0;
        double medianMs = 0.0;
        unsigned stutters = 0;

        double avgPackageW = 0.0;
        bool packageValid = false;

        double energyWh = 0.0;
        bool energyValid = false;

        double batteryStartPct = 0.0;
        double batteryEndPct = 0.0;
        double batteryDrawW = 0.0;
        bool batteryValid = false;

        double projectedHours = 0.0;
        bool projectionValid = false;

        double framesPerWattHour = 0.0;
        bool efficiencyValid = false;
    };

    capture_bi();

    bool active() const { return recording; }

    void start(const std::string &processName, DWORD processId);
    bool stop(summary_bi *out);
    void discard();

    // False when the fixed capture limit has been reached.
    bool addFrame(double intervalMs, LONGLONG time100ns);
    // False when the independent one-Hz power-sample limit is reached.
    bool addPowerSample(double packageW, bool packageValid,
                        double batteryPct, bool batteryPctValid,
                        double batteryRateW, bool batteryRateValid,
                        double fullChargedWh);

    double elapsedSeconds() const;
    size_t frameCount() const { return frames.count(); }
    DWORD targetProcessId() const { return targetPid; }
    double liveAverageFps() const { return frames.averageFps(); }
    double liveLow1Fps() const { return frames.lowFps(0.01); }
    bool liveLow1Valid() const { return frames.hasEnoughFor(0.01); }

    static std::wstring capturesDir();
    static bool loadIndex(std::vector<summary_bi> *out);

private:
    struct power_sample_bi
    {
        LONGLONG time100ns;
        double packageW;
        double batteryRateW;
        double batteryPct;
        bool packageValid;
        bool batteryRateValid;
        bool batteryPctValid;
    };

    bool writeFrameCsv(const std::wstring &path, DWORD *errorOut) const;
    bool appendIndex(const summary_bi &s) const;
    void computeSummary(summary_bi *out) const;

    bool recording;
    std::string processName;
    std::string startLabel;
    DWORD targetPid;
    LONGLONG startTime100ns;
    LONGLONG lastTime100ns;

    double fullChargedWh;

    frame_stats_bi frames;
    std::vector<power_sample_bi> power;
};

#endif
