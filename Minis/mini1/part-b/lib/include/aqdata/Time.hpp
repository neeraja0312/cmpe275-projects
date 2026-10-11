#pragma once

// Calendar <-> hour-index conversion. Times in the data are local *standard*
// time (no daylight saving), so every day has exactly 24 hours and an hour
// index is simply days * 24 + hour.
//
// daysFromCivil / civilFromDays are Howard Hinnant's public-domain algorithms
// (the same arithmetic std::chrono uses), written out so they are constexpr
// and cheap enough to run on every row.

#include "aqdata/Export.hpp"
#include "aqdata/Types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace aq {

constexpr std::int64_t daysFromCivil(std::int64_t y, unsigned m, unsigned d) noexcept {
    y -= m <= 2 ? 1 : 0;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

struct CivilDate {
    int year;
    unsigned month;
    unsigned day;
};

constexpr CivilDate civilFromDays(std::int64_t z) noexcept {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t y = static_cast<std::int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    return {static_cast<int>(y + (m <= 2 ? 1 : 0)), m, d};
}

inline constexpr std::int64_t kEpochDay = daysFromCivil(2021, 1, 1);

// Signed hours since 2021-01-01 00:00 (negative before the epoch).
constexpr std::int64_t hoursSinceEpoch(int year, unsigned month, unsigned day, unsigned hour) noexcept {
    return (daysFromCivil(year, month, day) - kEpochDay) * 24 + hour;
}

// 0 -> "2021-01-01T00"
AQ_API std::string formatHour(std::int64_t hour);

// Command-line time: "YYYY-MM-DD" (midnight) or "YYYY-MM-DDTHH".
AQ_API std::optional<HourIndex> parseHourArg(std::string_view text);

}  // namespace aq
