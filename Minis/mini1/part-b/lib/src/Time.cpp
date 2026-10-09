#include "aqdata/Time.hpp"

#include "Parse.hpp"

#include <array>
#include <cstdio>
#include <limits>

namespace aq {

std::string formatHour(std::int64_t hour) {
    std::int64_t days = hour / 24;
    std::int64_t hourOfDay = hour % 24;
    if (hourOfDay < 0) {  // floor division for hours before the epoch
        hourOfDay += 24;
        --days;
    }
    const CivilDate date = civilFromDays(kEpochDay + days);
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%04d-%02u-%02uT%02d", date.year, date.month, date.day,
                  static_cast<int>(hourOfDay));
    return buffer.data();
}

std::optional<HourIndex> parseHourArg(std::string_view text) {
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    unsigned hour = 0;
    if (text.size() == 13 && text[10] == 'T') {
        if (!parse::unsignedInt(text.substr(11, 2), hour) || hour > 23) return std::nullopt;
        text = text.substr(0, 10);
    } else if (text.size() != 10) {
        return std::nullopt;
    }
    if (!parse::date(text, year, month, day)) return std::nullopt;

    const std::int64_t hours = hoursSinceEpoch(year, month, day, hour);
    if (hours < 0 || hours > std::numeric_limits<HourIndex>::max()) return std::nullopt;
    return static_cast<HourIndex>(hours);
}

}  // namespace aq
