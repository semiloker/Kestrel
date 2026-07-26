#include <catch2/catch_test_macros.hpp>
#include <csv_bi.h>

#include <string>

TEST_CASE("CSV escaping is minimal and exact", "[csv][boundary]")
{
    REQUIRE(csv_bi::escape("plain text") == "plain text");
    REQUIRE(csv_bi::escape(" leading space ") == " leading space ");
    REQUIRE(csv_bi::escape("a,b") == "\"a,b\"");
    REQUIRE(csv_bi::escape("a\"b") == "\"a\"\"b\"");
    REQUIRE(csv_bi::escape("a\rb") == "\"a\rb\"");
    REQUIRE(csv_bi::escape("a\nb") == "\"a\nb\"");
}

TEST_CASE("CSV parser distinguishes the final empty field", "[csv][boundary]")
{
    const std::string line = ",";
    size_t position = 0;
    std::string value = "sentinel";

    REQUIRE(csv_bi::nextField(line, &position, &value));
    REQUIRE(value.empty());
    REQUIRE(position == 1);

    REQUIRE(csv_bi::nextField(line, &position, &value));
    REQUIRE(value.empty());
    REQUIRE(position > line.size());

    REQUIRE_FALSE(csv_bi::nextField(line, &position, &value));
}

TEST_CASE("CSV parser handles quotes at field boundaries", "[csv][boundary]")
{
    const std::string line = "\"a\"\"b\",\"\",tail";
    size_t position = 0;
    std::string value;

    REQUIRE(csv_bi::nextField(line, &position, &value));
    REQUIRE(value == "a\"b");

    REQUIRE(csv_bi::nextField(line, &position, &value));
    REQUIRE(value.empty());

    REQUIRE(csv_bi::nextField(line, &position, &value));
    REQUIRE(value == "tail");
    REQUIRE(position > line.size());
}

TEST_CASE("CSV parser validates arguments and cursor bounds", "[csv][strict]")
{
    const std::string line = "value";
    size_t position = 0;
    std::string value;

    REQUIRE_FALSE(csv_bi::nextField(line, nullptr, &value));
    REQUIRE_FALSE(csv_bi::nextField(line, &position, nullptr));

    position = line.size() + 1;
    REQUIRE_FALSE(csv_bi::nextField(line, &position, &value));
}

TEST_CASE("CSV parser rejects every non-comma suffix after a quote",
          "[csv][strict]")
{
    const std::string malformed[] = {
        "\"value\" ",
        "\"value\"\t",
        "\"value\"x",
        "\"value\"\r",
        "\"value\"\n"};

    for (const std::string &line : malformed)
    {
        size_t position = 0;
        std::string value;
        REQUIRE_FALSE(csv_bi::nextField(line, &position, &value));
    }
}

TEST_CASE("CSV parser does not accept quotes inside unquoted fields",
          "[csv][strict]")
{
    const std::string malformed[] = {
        "before\"after",
        "before\"after,next",
        "x,\"valid\",bad\"tail"};

    for (const std::string &line : malformed)
    {
        size_t position = 0;
        std::string value;
        bool rejected = false;

        while (position <= line.size())
        {
            if (!csv_bi::nextField(line, &position, &value))
            {
                rejected = true;
                break;
            }
        }
        REQUIRE(rejected);
    }
}
