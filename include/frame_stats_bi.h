#ifndef FRAME_STATS_BI_H
#define FRAME_STATS_BI_H

#include <windows.h>
#include <deque>
#include <vector>
#include <cstddef>

class frame_stats_bi
{
public:
    struct entry_bi
    {
        LONGLONG time100ns;
        float intervalMs;
    };

    frame_stats_bi();

    void reset();
    void push(double intervalMs, LONGLONG time100ns);
    void trimToWindow(LONGLONG now100ns, double seconds);
    void setCapacity(size_t maxFrames);

    size_t count() const { return frames.size(); }
    bool empty() const { return frames.empty(); }

    bool hasEnoughFor(double fraction) const;

    double averageMs() const;
    double averageFps() const;
    double medianMs() const;
    double minMs() const;
    double maxMs() const;

    double percentileMs(double p) const;
    double lowFps(double fraction) const;

    unsigned stutters() const;
    double spanSeconds() const;

    const std::deque<entry_bi> &samples() const { return frames; }

private:
    void rebuildScratch() const;
    void refreshStutterThreshold();

    std::deque<entry_bi> frames;
    size_t capacity;

    mutable std::vector<float> scratch;
    mutable bool scratchValid;

    std::vector<float> thresholdScratch;

    double sumMs;
    unsigned stutterCount;
    double stutterThresholdMs;
    size_t sinceThreshold;
};

#endif
