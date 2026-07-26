#include <catch2/catch_test_macros.hpp>
#include <csv_bi.h>

TEST_CASE("CSV fields round-trip without data loss", "[csv]")
{
    const std::string values[] = {
        "plain",
        "game,test.exe",
        "a \"quoted\" value",
        "",
        "unicode-\xd0\xb8\xd0\xb3\xd1\x80\xd0\xb0.exe"};

    for (const std::string &expected : values)
    {
        std::string line = csv_bi::escape(expected);
        std::string actual;
        size_t position = 0;
        REQUIRE(csv_bi::nextField(line, &position, &actual));
        REQUIRE(actual == expected);
        REQUIRE(position > line.size());
    }
}

TEST_CASE("CSV parser handles adjacent and quoted fields", "[csv]")
{
    std::string line = "one,\"two,too\",\"quote\"\"inside\",";
    const std::string expected[] = {
        "one", "two,too", "quote\"inside", ""};

    size_t position = 0;
    for (const std::string &value : expected)
    {
        std::string actual;
        REQUIRE(csv_bi::nextField(line, &position, &actual));
        REQUIRE(actual == value);
    }
    REQUIRE(position > line.size());
}

TEST_CASE("CSV parser rejects malformed quoting", "[csv]")
{
    std::string value;
    size_t position = 0;
    REQUIRE_FALSE(csv_bi::nextField("\"unterminated", &position, &value));

    position = 0;
    REQUIRE_FALSE(csv_bi::nextField("\"closed\"garbage", &position, &value));

    position = 0;
    REQUIRE_FALSE(csv_bi::nextField("un\"quoted", &position, &value));
}
