#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <frame_stats_bi.h>
#include <limits>

using Catch::Approx;

TEST_CASE("frame_stats_bi rejects invalid samples", "[frame_stats]")
{
    frame_stats_bi stats;

    stats.push(0.0, 1);
    stats.push(-1.0, 2);
    stats.push(10001.0, 3);
    stats.push(std::numeric_limits<double>::quiet_NaN(), 4);
    stats.push(std::numeric_limits<double>::infinity(), 5);

    REQUIRE(stats.empty());
}

TEST_CASE("frame_stats_bi computes summary values", "[frame_stats]")
{
    frame_stats_bi stats;
    stats.push(10.0, 10000000);
    stats.push(20.0, 20000000);
    stats.push(30.0, 30000000);

    REQUIRE(stats.count() == 3);
    REQUIRE(stats.averageMs() == Approx(20.0));
    REQUIRE(stats.averageFps() == Approx(50.0));
    REQUIRE(stats.minMs() == Approx(10.0));
    REQUIRE(stats.maxMs() == Approx(30.0));
    REQUIRE(stats.medianMs() == Approx(20.0));
    REQUIRE(stats.spanSeconds() == Approx(2.0));
}

TEST_CASE("frame_stats_bi keeps statistics coherent after eviction", "[frame_stats]")
{
    frame_stats_bi stats;
    stats.setCapacity(3);

    stats.push(5.0, 10000000);
    stats.push(10.0, 20000000);
    stats.push(20.0, 30000000);
    stats.push(40.0, 40000000);

    REQUIRE(stats.count() == 3);
    REQUIRE(stats.averageMs() == Approx(70.0 / 3.0));
    REQUIRE(stats.minMs() == Approx(10.0));
    REQUIRE(stats.maxMs() == Approx(40.0));
}

TEST_CASE("frame_stats_bi trims a time window coherently", "[frame_stats]")
{
    frame_stats_bi stats;
    stats.push(10.0, 10000000);
    stats.push(20.0, 20000000);
    stats.push(30.0, 30000000);

    stats.trimToWindow(30000000, 1.5);

    REQUIRE(stats.count() == 2);
    REQUIRE(stats.averageMs() == Approx(25.0));
    REQUIRE(stats.minMs() == Approx(20.0));
    REQUIRE(stats.maxMs() == Approx(30.0));
}

TEST_CASE("frame_stats_bi reports stutters for short and bounded runs", "[frame_stats]")
{
    frame_stats_bi stats;
    stats.push(16.0, 10000000);
    stats.push(16.0, 20000000);
    stats.push(16.0, 30000000);
    stats.push(50.0, 40000000);

    REQUIRE(stats.stutters() == 1);

    stats.setCapacity(1);
    REQUIRE(stats.stutters() == 0);
}

TEST_CASE("frame_stats_bi enforces low-percentile sample thresholds", "[frame_stats]")
{
    frame_stats_bi stats;
    for (int i = 0; i < 98; ++i)
        stats.push(10.0, i + 1);

    REQUIRE_FALSE(stats.hasEnoughFor(0.01));
    stats.push(20.0, 99);
    REQUIRE_FALSE(stats.hasEnoughFor(0.01));
    stats.push(20.0, 100);
    REQUIRE(stats.hasEnoughFor(0.01));
    REQUIRE(stats.lowFps(0.01) == Approx(50.0));

    for (int i = 100; i < 999; ++i)
        stats.push(10.0, i + 1);
    REQUIRE_FALSE(stats.hasEnoughFor(0.001));
    stats.push(30.0, 1000);
    REQUIRE(stats.hasEnoughFor(0.001));
}
