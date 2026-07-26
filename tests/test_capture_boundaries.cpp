#include <catch2/catch_test_macros.hpp>

#include <windows.h>
#include <cstdarg>
#include <deque>
#include <string>
#include <vector>

#include <frame_stats_bi.h>

// capture_bi deliberately exposes no persistence-independent power count.
// This test-only view lets the cap be reached without waiting six hours.
#define private public
#include <capture_bi.h>
#undef private

// The production test target does not link capture_bi.cpp. Include the real
// implementation here and provide inert path/log adapters below. Every test
// calls discard(), so no persistence path is reached.
#include "../src/capture_bi.cpp"

namespace paths_bi
{
    std::string wideToUtf8(const std::wstring &)
    {
        return std::string();
    }

    std::wstring utf8ToWide(const std::string &)
    {
        return std::wstring();
    }

    const std::wstring &dataDirWide()
    {
        static const std::wstring unavailable;
        return unavailable;
    }
}

namespace log_bi
{
    void write(const char *, ...)
    {
    }
}

TEST_CASE("capture ignores frames before recording and before its start time",
          "[capture][timestamp]")
{
    capture_bi capture;

    REQUIRE(capture.addFrame(16.0, 1));
    REQUIRE(capture.frameCount() == 0);

    capture.start("game.exe", 42);
    const LONGLONG started = capture.startTime100ns;

    REQUIRE(capture.addFrame(16.0, started - 1));
    REQUIRE(capture.frameCount() == 0);
    REQUIRE(capture.lastTime100ns == started);

    REQUIRE(capture.addFrame(16.0, started));
    REQUIRE(capture.frameCount() == 1);
    REQUIRE(capture.lastTime100ns == started);

    capture.discard();
    REQUIRE_FALSE(capture.active());
    REQUIRE(capture.frameCount() == 0);
}

TEST_CASE("capture frame cap rejects the first overflowing frame",
          "[capture][capacity]")
{
    capture_bi capture;
    capture.start("game.exe", 7);
    const LONGLONG started = capture.startTime100ns;

    bool allAccepted = true;
    for (size_t i = 0; i < capture_bi::MAX_CAPTURE_FRAMES; ++i)
    {
        if (!capture.addFrame(16.0, started + (LONGLONG)i + 1))
        {
            allAccepted = false;
            break;
        }
    }

    REQUIRE(allAccepted);
    REQUIRE(capture.frameCount() == capture_bi::MAX_CAPTURE_FRAMES);
    REQUIRE_FALSE(capture.addFrame(
        16.0, started + (LONGLONG)capture_bi::MAX_CAPTURE_FRAMES + 1));
    REQUIRE(capture.frameCount() == capture_bi::MAX_CAPTURE_FRAMES);

    capture.discard();
    REQUIRE(capture.frameCount() == 0);
}

TEST_CASE("capture power cap drops samples before updating capacity state",
          "[capture][capacity]")
{
    capture_bi capture;
    capture.start("game.exe", 9);

    capture.power.resize(capture_bi::MAX_POWER_SAMPLES);
    capture.power.back().time100ns = 0;

    REQUIRE(capture.power.size() == capture_bi::MAX_POWER_SAMPLES);
    REQUIRE(capture.fullChargedWh == 0.0);

    REQUIRE_FALSE(
        capture.addPowerSample(25.0, true, 80.0, true, -15.0, true, 75.0));

    REQUIRE(capture.power.size() == capture_bi::MAX_POWER_SAMPLES);
    REQUIRE(capture.fullChargedWh == 0.0);

    capture.discard();
    REQUIRE(capture.power.empty());
}

TEST_CASE("capture invalid frames do not advance its last timestamp",
          "[capture][timestamp]")
{
    capture_bi capture;
    capture.start("game.exe", 11);
    const LONGLONG started = capture.startTime100ns;

    REQUIRE(capture.addFrame(0.0, started + 100));
    REQUIRE(capture.addFrame(10001.0, started + 200));
    REQUIRE(capture.frameCount() == 0);
    REQUIRE(capture.lastTime100ns == started);

    capture.discard();
}
