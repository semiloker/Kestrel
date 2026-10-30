#include <catch2/catch_test_macros.hpp>
#include <i18n_bi.h>

using i18n_bi::formatCompatible;

TEST_CASE("translations keep the fallback's conversions", "[i18n]")
{
    CHECK(formatCompatible(L"%.0f%% \u635F\u8017", L"%.0f%% wear"));
    CHECK(formatCompatible(L"\u7248\u672C %d.%d.%d", L"Version %d.%d.%d"));
    CHECK(formatCompatible(L"\u5269\u4F59 %ls", L"%ls left"));
    CHECK(formatCompatible(L"no conversions", L"none here either"));
    CHECK(formatCompatible(L"", L""));
}

TEST_CASE("literal percent signs are not conversions", "[i18n]")
{
    CHECK(formatCompatible(L"100%%", L"all"));
    CHECK(formatCompatible(L"%d%%", L"%d"));
}

TEST_CASE("translations that would misread the arguments are rejected", "[i18n]")
{
    CHECK_FALSE(formatCompatible(L"%s", L"%d cycles"));
    CHECK_FALSE(formatCompatible(L"%n", L"%d cycles"));
    CHECK_FALSE(formatCompatible(L"%d %d", L"%d cycles"));
    CHECK_FALSE(formatCompatible(L"cycles", L"%d cycles"));
    CHECK_FALSE(formatCompatible(L"%u", L"%d cycles"));
    CHECK_FALSE(formatCompatible(L"%.0f %u", L"%u %.0f"));
    CHECK_FALSE(formatCompatible(L"%*d", L"%d"));
    CHECK_FALSE(formatCompatible(L"%1$d", L"%d"));
    CHECK_FALSE(formatCompatible(L"%5000d", L"%d"));
}

TEST_CASE("a stray percent sign is rejected", "[i18n]")
{
    CHECK_FALSE(formatCompatible(L"100%", L"all"));
    CHECK_FALSE(formatCompatible(L"100% done", L"all done"));
}
