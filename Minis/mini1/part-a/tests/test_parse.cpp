// Field parsers, time conversion and formatting.

#include "Parse.hpp"
#include "Time.hpp"
#include "TestUtil.hpp"
#include "Types.hpp"

#include <cstdint>
#include <string>

using namespace aq;

namespace {

std::int32_t scaled(const char* text, int decimals) {
    std::int32_t out = -999999;
    return parse::scaled(text, decimals, out) ? out : -999999;
}

bool scaledFails(const char* text, int decimals) {
    std::int32_t out = 0;
    return !parse::scaled(text, decimals, out);
}

}  // namespace

int main() {
    // Exact decimal -> scaled integer (ozone 3 decimals, NO2 1 decimal).
    CHECK_EQ(scaled("0.062", 3), 62);
    CHECK_EQ(scaled("0.070", 3), 70);
    CHECK_EQ(scaled("0.379", 3), 379);
    CHECK_EQ(scaled("-0.005", 3), -5);
    CHECK_EQ(scaled("0", 3), 0);
    CHECK_EQ(scaled("5", 3), 5000);
    CHECK_EQ(scaled(".5", 1), 5);
    CHECK_EQ(scaled("29.5", 1), 295);
    CHECK_EQ(scaled("-4.9", 1), -49);
    CHECK_EQ(scaled("141.3", 1), 1413);
    CHECK_EQ(scaled("0.0625", 3), 63);    // rounds half away from zero
    CHECK_EQ(scaled("-0.0625", 3), -63);
    CHECK_EQ(scaled("0.0624", 3), 62);
    CHECK_EQ(scaled("0.019", 3), 19);
    CHECK(scaledFails("", 3));
    CHECK(scaledFails("-", 3));
    CHECK(scaledFails("abc", 3));
    CHECK(scaledFails("1.2.3", 3));
    CHECK(scaledFails("1e3", 3));

    // Integers: whole field only.
    std::uint16_t county = 0;
    CHECK(parse::unsignedInt("073", county) && county == 73);
    CHECK(!parse::unsignedInt("", county));
    CHECK(!parse::unsignedInt("12a", county));
    CHECK(!parse::unsignedInt("-1", county));
    std::uint8_t state = 0;
    CHECK(!parse::unsignedInt("300", state));  // does not fit in 8 bits

    // Dates and hours.
    int y = 0;
    unsigned m = 0, d = 0, h = 0;
    CHECK(parse::date("2024-03-01", y, m, d) && y == 2024 && m == 3 && d == 1);
    CHECK(!parse::date("2024-3-01", y, m, d));
    CHECK(!parse::date("2024-13-01", y, m, d));
    CHECK(parse::hourOfDay("13:00", h) && h == 13);
    CHECK(!parse::hourOfDay("13:30", h));
    CHECK(!parse::hourOfDay("24:00", h));

    // Hour index: hours since 2021-01-01 00:00 local standard time.
    CHECK_EQ(hoursSinceEpoch(2021, 1, 1, 0), 0);
    CHECK_EQ(hoursSinceEpoch(2021, 1, 2, 5), 29);
    CHECK_EQ(hoursSinceEpoch(2022, 1, 1, 0), 365 * 24);
    CHECK_EQ(hoursSinceEpoch(2024, 3, 1, 1) - hoursSinceEpoch(2024, 2, 28, 1), 48);  // 2024 is a leap year
    CHECK_EQ(formatHour(0), std::string("2021-01-01T00"));
    CHECK_EQ(formatHour(hoursSinceEpoch(2024, 3, 1, 1)), std::string("2024-03-01T01"));
    CHECK_EQ(formatHour(hoursSinceEpoch(2026, 5, 31, 23)), std::string("2026-05-31T23"));
    CHECK_EQ(formatHour(-1), std::string("2020-12-31T23"));
    CHECK(parseHourArg("2024-03-01") == static_cast<HourIndex>(hoursSinceEpoch(2024, 3, 1, 0)));
    CHECK(parseHourArg("2024-03-01T13") == static_cast<HourIndex>(hoursSinceEpoch(2024, 3, 1, 13)));
    CHECK(!parseHourArg("2020-12-31"));  // before the epoch
    CHECK(!parseHourArg("2024-03-01T24"));
    for (std::int64_t hour : {0LL, 1LL, 8783LL, 43823LL, 47447LL}) {
        CHECK(parseHourArg(formatHour(hour)) == static_cast<HourIndex>(hour));  // round trip
    }

    // Monitor keys.
    const auto key = parseMonitorKey("01-073-0023-42602-1");
    CHECK(key.has_value());
    if (key) {
        CHECK_EQ(int{key->state}, 1);
        CHECK_EQ(key->county, 73);
        CHECK_EQ(key->site, 23);
        CHECK(key->pollutant == Pollutant::NO2);
        CHECK_EQ(int{key->poc}, 1);
        CHECK_EQ(toString(*key), std::string("01-073-0023-42602-1"));
    }
    CHECK(!parseMonitorKey("01-073-0023-42602"));
    CHECK(!parseMonitorKey("01-073-0023-42602-1-9"));
    CHECK(!parseMonitorKey("01-073-0023-88101-1"));  // not one of our pollutants

    // Formatting.
    CHECK_EQ(formatScaled(-5, 3), std::string("-0.005"));
    CHECK_EQ(formatScaled(62, 3), std::string("0.062"));
    CHECK_EQ(formatScaled(1413, 1), std::string("141.3"));
    CHECK_EQ(formatScaled(0, 1), std::string("0.0"));

    return testutil::finish("test_parse");
}
