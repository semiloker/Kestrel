#include <catch2/catch_test_macros.hpp>
#include <hud_bi.h>

TEST_CASE("hud_series_bi construction", "[hud_series]")
{
    SECTION("default capacity")
    {
        hud_series_bi s;
        REQUIRE(s.size() == 0);
        REQUIRE(s.current() == 0.0);
        REQUIRE(s.minimum() == 0.0);
        REQUIRE(s.maximum() == 0.0);
    }

    SECTION("explicit capacity")
    {
        hud_series_bi s(100);
        REQUIRE(s.size() == 0);
    }

    SECTION("zero capacity becomes one")
    {
        hud_series_bi s(0);
        REQUIRE(s.size() == 0);
        s.push(42.0);
        REQUIRE(s.size() == 1);
        REQUIRE(s.current() == 42.0);
    }

    SECTION("capacity of one")
    {
        hud_series_bi s(1);
        s.push(10.0);
        REQUIRE(s.size() == 1);
        REQUIRE(s.at(0) == 10.0);
        s.push(20.0);
        REQUIRE(s.size() == 1);
        REQUIRE(s.at(0) == 20.0);
    }
}

TEST_CASE("hud_series_bi push and access", "[hud_series]")
{
    SECTION("push single value")
    {
        hud_series_bi s(10);
        s.push(3.14);
        REQUIRE(s.size() == 1);
        REQUIRE(s.current() == 3.14);
        REQUIRE(s.at(0) == 3.14);
        REQUIRE(s.minimum() == 3.14);
        REQUIRE(s.maximum() == 3.14);
    }

    SECTION("push multiple values - increasing")
    {
        hud_series_bi s(10);
        s.push(1.0);
        s.push(2.0);
        s.push(3.0);
        REQUIRE(s.size() == 3);
        REQUIRE(s.current() == 3.0);
        REQUIRE(s.minimum() == 1.0);
        REQUIRE(s.maximum() == 3.0);
        REQUIRE(s.at(0) == 1.0);
        REQUIRE(s.at(1) == 2.0);
        REQUIRE(s.at(2) == 3.0);
    }

    SECTION("push multiple values - decreasing")
    {
        hud_series_bi s(10);
        s.push(5.0);
        s.push(4.0);
        s.push(3.0);
        REQUIRE(s.minimum() == 3.0);
        REQUIRE(s.maximum() == 5.0);
    }

    SECTION("push exactly capacity")
    {
        hud_series_bi s(3);
        s.push(1.0);
        s.push(2.0);
        s.push(3.0);
        REQUIRE(s.size() == 3);
        REQUIRE(s.at(0) == 1.0);
        REQUIRE(s.at(1) == 2.0);
        REQUIRE(s.at(2) == 3.0);
    }

    SECTION("push beyond capacity wraps buffer")
    {
        hud_series_bi s(3);
        s.push(1.0);
        s.push(2.0);
        s.push(3.0);
        s.push(4.0);
        REQUIRE(s.size() == 3);
        REQUIRE(s.at(0) == 2.0);
        REQUIRE(s.at(1) == 3.0);
        REQUIRE(s.at(2) == 4.0);
    }

    SECTION("min/max update after wrap evicts extremes")
    {
        hud_series_bi s(3);
        s.push(10.0);
        s.push(1.0);
        s.push(100.0);
        REQUIRE(s.minimum() == 1.0);
        REQUIRE(s.maximum() == 100.0);
        s.push(50.0);
        REQUIRE(s.size() == 3);
        REQUIRE(s.minimum() == 1.0);
        REQUIRE(s.maximum() == 100.0);
    }
}

TEST_CASE("hud_series_bi at() bounds", "[hud_series]")
{
    SECTION("out of bounds returns 0.0")
    {
        hud_series_bi s(10);
        s.push(5.0);
        REQUIRE(s.at(0) == 5.0);
        REQUIRE(s.at(1) == 0.0);
        REQUIRE(s.at(999) == 0.0);
    }

    SECTION("at() on empty series")
    {
        hud_series_bi s(10);
        REQUIRE(s.at(0) == 0.0);
    }
}

TEST_CASE("hud_series_bi reset", "[hud_series]")
{
    SECTION("reset clears state")
    {
        hud_series_bi s(10);
        s.push(1.0);
        s.push(2.0);
        s.reset();
        REQUIRE(s.size() == 0);
        REQUIRE(s.current() == 0.0);
        REQUIRE(s.minimum() == 0.0);
        REQUIRE(s.maximum() == 0.0);
    }

    SECTION("reset then push reuses capacity")
    {
        hud_series_bi s(3);
        s.push(1.0);
        s.push(2.0);
        s.push(3.0);
        s.push(4.0);
        REQUIRE(s.size() == 3);
        s.reset();
        REQUIRE(s.size() == 0);
        s.push(99.0);
        REQUIRE(s.size() == 1);
        REQUIRE(s.at(0) == 99.0);
        REQUIRE(s.current() == 99.0);
    }
}

TEST_CASE("hud_series_bi percentile", "[hud_series]")
{
    SECTION("empty series returns 0.0")
    {
        hud_series_bi s(10);
        REQUIRE(s.percentile(0.5) == 0.0);
    }

    SECTION("single element")
    {
        hud_series_bi s(10);
        s.push(42.0);
        REQUIRE(s.percentile(0.0) == 42.0);
        REQUIRE(s.percentile(0.5) == 42.0);
        REQUIRE(s.percentile(1.0) == 42.0);
    }

    SECTION("median of odd count")
    {
        hud_series_bi s(10);
        s.push(1.0);
        s.push(3.0);
        s.push(2.0);
        REQUIRE(s.percentile(0.5) == 2.0);
    }

    SECTION("p clamped to [0,1]")
    {
        hud_series_bi s(10);
        s.push(5.0);
        s.push(10.0);
        REQUIRE(s.percentile(-0.5) == 5.0);
        REQUIRE(s.percentile(1.5) == 10.0);
    }

    SECTION("percentile after reset")
    {
        hud_series_bi s(10);
        s.push(100.0);
        s.reset();
        REQUIRE(s.percentile(0.5) == 0.0);
    }
}

TEST_CASE("hud_series_bi values not modified by percentile", "[hud_series]")
{
    hud_series_bi s(10);
    s.push(1.0);
    s.push(2.0);
    s.push(3.0);
    s.percentile(0.5);
    REQUIRE(s.size() == 3);
    REQUIRE(s.at(0) == 1.0);
    REQUIRE(s.at(1) == 2.0);
    REQUIRE(s.at(2) == 3.0);
}
