#pragma once

// Core value types shared by every part of the library.
//
// Terminology (EPA AQS):
//   Site    = State Code + County Code + Site Num (a physical station)
//   Monitor = Site + Parameter Code + POC (one instrument, one hourly time series)

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aq {

// The two EPA AQS parameters in the dataset.
enum class Pollutant : std::uint8_t { Ozone = 0, NO2 = 1 };

inline constexpr std::size_t kPollutantCount = 2;

constexpr std::uint32_t parameterCode(Pollutant p) {
    return p == Pollutant::Ozone ? 44201u : 42602u;
}

// Readings are stored as integers: ozone in ppm x 1000, NO2 in ppb x 10.
// This is exact for the data (ozone has at most 3 decimals, NO2 at most 1),
// so threshold comparisons never suffer floating-point rounding.
constexpr int valueDecimals(Pollutant p) { return p == Pollutant::Ozone ? 3 : 1; }
constexpr int valueScale(Pollutant p) { return p == Pollutant::Ozone ? 1000 : 10; }

constexpr std::string_view pollutantName(Pollutant p) {
    return p == Pollutant::Ozone ? "ozone" : "no2";
}
constexpr std::string_view unitName(Pollutant p) {
    return p == Pollutant::Ozone ? "ppm" : "ppb";
}

std::optional<Pollutant> pollutantFromCode(std::uint32_t code);
// Accepts "ozone", "o3", "44201", "no2", "42602".
std::optional<Pollutant> pollutantFromName(std::string_view name);

using MonitorId = std::uint16_t;   // index into the MonitorRegistry
using HourIndex = std::uint32_t;   // hours since 2021-01-01 00:00 local standard time
using ScaledValue = std::int16_t;  // reading x valueScale(pollutant)
using CodeId = std::uint8_t;       // id from a Dictionary; 0 = empty

// One hourly reading. Everything that repeats per monitor lives in Monitor.
struct Measurement {
    HourIndex hour;
    MonitorId monitor;
    ScaledValue value;
    CodeId qualifier;  // 0 = no qualifier
    CodeId method;     // EPA method code
};
static_assert(sizeof(Measurement) == 12, "Measurement is expected to pack into 12 bytes");

struct MonitorKey {
    std::uint8_t state = 0;
    std::uint16_t county = 0;
    std::uint16_t site = 0;
    Pollutant pollutant = Pollutant::Ozone;
    std::uint8_t poc = 0;

    constexpr std::uint64_t packed() const {
        return (std::uint64_t{state} << 48U) | (std::uint64_t{county} << 32U) |
               (std::uint64_t{site} << 16U) |
               (std::uint64_t{static_cast<std::uint8_t>(pollutant)} << 8U) | std::uint64_t{poc};
    }
    bool operator==(const MonitorKey&) const = default;
};

// "01-073-0023-42602-1"
std::string toString(const MonitorKey& key);
std::optional<MonitorKey> parseMonitorKey(std::string_view text);

// Per-monitor attributes, stored once instead of on every row (Flyweight).
struct Monitor {
    MonitorKey key;
    double latitude = 0.0;
    double longitude = 0.0;
    std::string datum;
    std::string stateName;
    std::string countyName;
    std::string methodType;
    std::int8_t utcOffsetHours = 0;  // local standard time minus GMT
    std::uint64_t rows = 0;
    HourIndex firstHour = std::numeric_limits<HourIndex>::max();
    HourIndex lastHour = 0;
};

// Half-open range of hours [begin, end).
struct HourRange {
    HourIndex begin = 0;
    HourIndex end = std::numeric_limits<HourIndex>::max();

    constexpr bool contains(HourIndex h) const { return h >= begin && h < end; }
    constexpr bool overlaps(HourIndex first, HourIndex last) const {
        return last >= begin && first < end;
    }
    constexpr bool covers(HourIndex first, HourIndex last) const {
        return first >= begin && last < end;
    }
};

// Q2: readings of one pollutant with lo <= value <= hi, optionally within a time range.
struct ValueQuery {
    Pollutant pollutant = Pollutant::Ozone;
    ScaledValue lo = std::numeric_limits<ScaledValue>::min();
    ScaledValue hi = std::numeric_limits<ScaledValue>::max();
    HourRange range;
};

enum class GroupBy : std::uint8_t { None, Monitor, HourOfDay };

// Q3: count / min / max / mean of one pollutant over a time range.
struct AggregateQuery {
    Pollutant pollutant = Pollutant::Ozone;
    HourRange range;
    GroupBy groupBy = GroupBy::None;
};

// Running statistics in scaled units. Sums are exact integers.
struct Stats {
    std::uint64_t count = 0;
    std::int64_t sum = 0;
    ScaledValue min = std::numeric_limits<ScaledValue>::max();
    ScaledValue max = std::numeric_limits<ScaledValue>::min();

    void add(ScaledValue v) {
        ++count;
        sum += v;
        if (v < min) min = v;
        if (v > max) max = v;
    }
    void merge(const Stats& other) {
        count += other.count;
        sum += other.sum;
        if (other.min < min) min = other.min;
        if (other.max > max) max = other.max;
    }
    double mean() const { return count == 0 ? 0.0 : static_cast<double>(sum) / static_cast<double>(count); }
};

struct AggregateResult {
    Pollutant pollutant = Pollutant::Ozone;
    GroupBy groupBy = GroupBy::None;
    std::vector<Stats> groups;  // 1 (None), one per monitor id (Monitor), or 24 (HourOfDay)
};

// Formats a scaled integer with a fixed number of decimals: (-5, 3) -> "-0.005".
std::string formatScaled(std::int32_t value, int decimals);

}  // namespace aq
