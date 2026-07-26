#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <frame_stats_bi.h>

#include <cmath>
#include <limits>

using Catch::Approx;

TEST_CASE("frame_stats_bi accepts the documented interval boundaries",
          "[frame_stats][boundary]")
{
    frame_stats_bi stats;

    stats.push((double)(std::numeric_limits<float>::min)(), 1);
    stats.push(10000.0, 2);

    REQUIRE(stats.count() == 2);
    REQUIRE(stats.samples().front().intervalMs > 0.0f);
    REQUIRE(stats.samples().back().intervalMs == 10000.0f);

    stats.push(std::nextafter(10000.0, std::numeric_limits<double>::infinity()), 3);
    REQUIRE(stats.count() == 2);
}

TEST_CASE("frame_stats_bi empty and invalid percentile queries are safe",
          "[frame_stats][boundary]")
{
    frame_stats_bi stats;

    REQUIRE(stats.averageMs() == 0.0);
    REQUIRE(stats.averageFps() == 0.0);
    REQUIRE(stats.percentileMs(0.5) == 0.0);
    REQUIRE(stats.medianMs() == 0.0);
    REQUIRE(stats.minMs() == 0.0);
    REQUIRE(stats.maxMs() == 0.0);
    REQUIRE(stats.lowFps(0.0) == 0.0);
    REQUIRE(stats.lowFps(1.0) == 0.0);
    REQUIRE(stats.lowFps(-0.01) == 0.0);
    REQUIRE(stats.stutters() == 0);
    REQUIRE(stats.spanSeconds() == 0.0);
}

TEST_CASE("frame_stats_bi clamps percentiles and rounds to the nearest rank",
          "[frame_stats][boundary]")
{
    frame_stats_bi stats;
    stats.push(10.0, 1);
    stats.push(20.0, 2);
    stats.push(30.0, 3);
    stats.push(40.0, 4);

    REQUIRE(stats.percentileMs(-1.0) == Approx(10.0));
    REQUIRE(stats.percentileMs(0.0) == Approx(10.0));
    REQUIRE(stats.percentileMs(0.5) == Approx(30.0));
    REQUIRE(stats.percentileMs(1.0) == Approx(40.0));
    REQUIRE(stats.percentileMs(2.0) == Approx(40.0));
}

TEST_CASE("frame_stats_bi zero capacity retains exactly the newest sample",
          "[frame_stats][capacity]")
{
    frame_stats_bi stats;
    stats.setCapacity(0);

    stats.push(10.0, 1);
    stats.push(25.0, 2);

    REQUIRE(stats.count() == 1);
    REQUIRE(stats.samples().front().time100ns == 2);
    REQUIRE(stats.averageMs() == Approx(25.0));
    REQUIRE(stats.minMs() == Approx(25.0));
    REQUIRE(stats.maxMs() == Approx(25.0));
}

TEST_CASE("frame_stats_bi trimming retains the exact cutoff",
          "[frame_stats][window]")
{
    frame_stats_bi stats;
    stats.push(10.0, 10000000);
    stats.push(20.0, 20000000);
    stats.push(30.0, 30000000);

    stats.trimToWindow(30000000, 1.0);

    REQUIRE(stats.count() == 2);
    REQUIRE(stats.samples().front().time100ns == 20000000);
    REQUIRE(stats.averageMs() == Approx(25.0));

    stats.trimToWindow(30000000, 0.0);
    stats.trimToWindow(30000000, -1.0);
    REQUIRE(stats.count() == 2);
}

TEST_CASE("frame_stats_bi never reports a negative timestamp span",
          "[frame_stats][timestamp]")
{
    frame_stats_bi stats;
    stats.push(10.0, 20);
    stats.push(10.0, 10);

    REQUIRE(stats.spanSeconds() == 0.0);
}
