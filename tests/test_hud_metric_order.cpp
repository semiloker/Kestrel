#include <catch2/catch_test_macros.hpp>
#include <hud_bi.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    std::string join(const std::vector<int> &ids)
    {
        std::string out;
        for (size_t i = 0; i < ids.size(); ++i)
        {
            if (i)
                out += ",";
            out += std::to_string(ids[i]);
        }
        return out;
    }

    bool isPermutationOfAllIds(std::vector<int> ids)
    {
        if (ids.size() != (size_t)HUD_M_COUNT)
            return false;
        std::sort(ids.begin(), ids.end());
        for (int i = 0; i < HUD_M_COUNT; ++i)
        {
            if (ids[(size_t)i] != i)
                return false;
        }
        return true;
    }
}

TEST_CASE("hud_parseMetricOrder accepts a complete order", "[hud_order]")
{
    std::vector<int> saved;
    for (int i = HUD_M_COUNT - 1; i >= 0; --i)
        saved.push_back(i);

    std::vector<int> out;
    REQUIRE(hud_parseMetricOrder(join(saved), out));
    REQUIRE(out == saved);
}

TEST_CASE("hud_parseMetricOrder returns an empty order for empty text", "[hud_order]")
{
    std::vector<int> out;
    REQUIRE(hud_parseMetricOrder("", out));
    REQUIRE(out.empty());
}

// The regression this exists for: an order written by an older build lists only
// the metrics that existed then. It used to be discarded wholesale, silently
// resetting the arrangement every time a metric was appended to the enum.
TEST_CASE("hud_parseMetricOrder keeps a short order and appends what is new", "[hud_order]")
{
    const int missingCount = 2;
    REQUIRE(HUD_M_COUNT > missingCount);

    std::vector<int> saved;
    for (int i = HUD_M_COUNT - missingCount - 1; i >= 0; --i)
        saved.push_back(i);

    std::vector<int> out;
    REQUIRE(hud_parseMetricOrder(join(saved), out));

    REQUIRE(out.size() == (size_t)HUD_M_COUNT);
    REQUIRE(isPermutationOfAllIds(out));

    SECTION("the saved arrangement survives untouched at the front")
    {
        std::vector<int> prefix(out.begin(), out.begin() + (long)saved.size());
        REQUIRE(prefix == saved);
    }

    SECTION("the new metrics land at the end in enum order")
    {
        std::vector<int> appended(out.begin() + (long)saved.size(), out.end());
        std::vector<int> expected;
        for (int i = HUD_M_COUNT - missingCount; i < HUD_M_COUNT; ++i)
            expected.push_back(i);
        REQUIRE(appended == expected);
    }
}

TEST_CASE("hud_parseMetricOrder completes an order holding a single id", "[hud_order]")
{
    std::vector<int> out;
    REQUIRE(hud_parseMetricOrder("3", out));
    REQUIRE(out.size() == (size_t)HUD_M_COUNT);
    REQUIRE(out[0] == 3);
    REQUIRE(isPermutationOfAllIds(out));
}

TEST_CASE("hud_parseMetricOrder rejects malformed text", "[hud_order]")
{
    std::vector<int> out;

    SECTION("non-numeric token")
    {
        REQUIRE_FALSE(hud_parseMetricOrder("0,fps,2", out));
        REQUIRE(out.empty());
    }

    SECTION("trailing garbage on a number")
    {
        REQUIRE_FALSE(hud_parseMetricOrder("0,1x", out));
        REQUIRE(out.empty());
    }

    SECTION("empty token")
    {
        REQUIRE_FALSE(hud_parseMetricOrder("0,,1", out));
        REQUIRE(out.empty());
    }

    SECTION("negative id")
    {
        REQUIRE_FALSE(hud_parseMetricOrder("0,-1", out));
        REQUIRE(out.empty());
    }

    SECTION("id past the end of the enum")
    {
        REQUIRE_FALSE(hud_parseMetricOrder("0," + std::to_string(HUD_M_COUNT), out));
        REQUIRE(out.empty());
    }

    SECTION("duplicate id")
    {
        REQUIRE_FALSE(hud_parseMetricOrder("0,1,1", out));
        REQUIRE(out.empty());
    }
}

TEST_CASE("hud_parseMetricOrder tolerates padding around tokens", "[hud_order]")
{
    std::vector<int> out;
    REQUIRE(hud_parseMetricOrder(" 2 , 0 ,1 ", out));
    REQUIRE(out.size() == (size_t)HUD_M_COUNT);
    REQUIRE(out[0] == 2);
    REQUIRE(out[1] == 0);
    REQUIRE(out[2] == 1);
    REQUIRE(isPermutationOfAllIds(out));
}
