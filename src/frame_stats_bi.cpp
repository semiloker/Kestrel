#include "frame_stats_bi.h"

#include <algorithm>

namespace
{
    const size_t DEFAULT_CAPACITY = 240000;
    const double STUTTER_FACTOR = 2.0;
}

frame_stats_bi::frame_stats_bi()
    : capacity(DEFAULT_CAPACITY),
      scratchValid(false),
      sumMs(0.0),
      stutterCount(0),
      stutterThresholdMs(0.0),
      sinceThreshold(0)
{
}

void frame_stats_bi::reset()
{
    frames.clear();
    scratch.clear();
    scratchValid = false;
    sumMs = 0.0;
    stutterCount = 0;
    stutterThresholdMs = 0.0;
    sinceThreshold = 0;
}

void frame_stats_bi::setCapacity(size_t maxFrames)
{
    capacity = maxFrames > 0 ? maxFrames : 1;

    while (frames.size() > capacity)
    {
        sumMs -= frames.front().intervalMs;
        frames.pop_front();
        scratchValid = false;
    }
}

void frame_stats_bi::push(double intervalMs, LONGLONG time100ns)
{
    if (intervalMs <= 0.0 || intervalMs > 10000.0)
        return;

    entry_bi e;
    e.time100ns = time100ns;
    e.intervalMs = (float)intervalMs;

    frames.push_back(e);
    sumMs += intervalMs;
    scratchValid = false;

    if (stutterThresholdMs > 0.0 && intervalMs > stutterThresholdMs)
        ++stutterCount;

    if (frames.size() > capacity)
    {
        sumMs -= frames.front().intervalMs;
        frames.pop_front();
    }

    ++sinceThreshold;

    if (sinceThreshold >= 600)
    {
        sinceThreshold = 0;
        refreshStutterThreshold();
    }
}

void frame_stats_bi::refreshStutterThreshold()
{
    const size_t WINDOW = 2000;

    size_t n = frames.size() < WINDOW ? frames.size() : WINDOW;
    if (n < 30)
        return;

    thresholdScratch.resize(n);

    std::deque<entry_bi>::const_reverse_iterator it = frames.rbegin();
    for (size_t i = 0; i < n; ++i, ++it)
        thresholdScratch[i] = it->intervalMs;

    size_t mid = n / 2;
    std::nth_element(thresholdScratch.begin(), thresholdScratch.begin() + mid,
                     thresholdScratch.end());

    double median = thresholdScratch[mid];
    if (median > 0.0)
        stutterThresholdMs = median * STUTTER_FACTOR;
}

void frame_stats_bi::trimToWindow(LONGLONG now100ns, double seconds)
{
    if (seconds <= 0.0)
        return;

    LONGLONG cutoff = now100ns - (LONGLONG)(seconds * 10000000.0);

    while (!frames.empty() && frames.front().time100ns < cutoff)
    {
        sumMs -= frames.front().intervalMs;
        frames.pop_front();
        scratchValid = false;
    }
}

bool frame_stats_bi::hasEnoughFor(double fraction) const
{
    if (fraction <= 0.0)
        return false;

    return (double)frames.size() >= (1.0 / fraction);
}

double frame_stats_bi::averageMs() const
{
    if (frames.empty())
        return 0.0;

    return sumMs / (double)frames.size();
}

double frame_stats_bi::averageFps() const
{
    double avg = averageMs();
    return avg > 0.0 ? 1000.0 / avg : 0.0;
}

void frame_stats_bi::rebuildScratch() const
{
    scratch.resize(frames.size());

    size_t i = 0;
    for (std::deque<entry_bi>::const_iterator it = frames.begin(); it != frames.end(); ++it)
        scratch[i++] = it->intervalMs;

    scratchValid = true;
}

double frame_stats_bi::percentileMs(double p) const
{
    if (frames.empty())
        return 0.0;

    if (p < 0.0)
        p = 0.0;
    if (p > 1.0)
        p = 1.0;

    if (!scratchValid)
        rebuildScratch();

    std::vector<float> work(scratch);

    size_t index = (size_t)(p * (double)(work.size() - 1) + 0.5);
    if (index >= work.size())
        index = work.size() - 1;

    std::nth_element(work.begin(), work.begin() + index, work.end());
    return work[index];
}

double frame_stats_bi::lowFps(double fraction) const
{
    if (fraction <= 0.0 || fraction >= 1.0)
        return 0.0;

    double ms = percentileMs(1.0 - fraction);
    return ms > 0.0 ? 1000.0 / ms : 0.0;
}

double frame_stats_bi::medianMs() const
{
    return percentileMs(0.5);
}

double frame_stats_bi::minMs() const
{
    if (frames.empty())
        return 0.0;

    if (!scratchValid)
        rebuildScratch();

    return *std::min_element(scratch.begin(), scratch.end());
}

double frame_stats_bi::maxMs() const
{
    if (frames.empty())
        return 0.0;

    if (!scratchValid)
        rebuildScratch();

    return *std::max_element(scratch.begin(), scratch.end());
}

unsigned frame_stats_bi::stutters() const
{
    return stutterCount;
}

double frame_stats_bi::spanSeconds() const
{
    if (frames.size() < 2)
        return 0.0;

    LONGLONG span = frames.back().time100ns - frames.front().time100ns;
    return span > 0 ? (double)span / 10000000.0 : 0.0;
}
