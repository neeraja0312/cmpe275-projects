// End-to-end check on a small synthetic dataset written in the EPA format.
// Every loader version must load it identically, and every query (in every
// result mode) must match a brute-force answer computed from the generated rows.

#include "AirQualityDB.hpp"
#include "TestUtil.hpp"
#include "Time.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>

using namespace aq;
namespace fs = std::filesystem;

namespace {

struct TestMonitor {
    unsigned state, county, site;
    Pollutant pollutant;
    unsigned poc;
    double lat, lon;
    int utcOffset;
    const char* stateName;
    const char* countyName;
};

// Two ozone instruments at one Los Angeles site, one in New York, one NO2 in Los Angeles.
const TestMonitor kMonitors[] = {
    {6, 37, 1103, Pollutant::Ozone, 1, 34.06659, -118.22688, -8, "California", "Los Angeles"},
    {6, 37, 1103, Pollutant::Ozone, 2, 34.06659, -118.22688, -8, "California", "Los Angeles"},
    {36, 61, 135, Pollutant::Ozone, 1, 40.81976, -73.94825, -5, "New York", "New York"},
    {6, 37, 1103, Pollutant::NO2, 1, 34.06659, -118.22688, -8, "California", "Los Angeles"},
};

struct Expected {
    std::size_t monitor;  // index into kMonitors
    std::int64_t hour;
    int value;  // scaled
};

std::string pad(unsigned value, int width) {
    std::ostringstream out;
    out << std::setw(width) << std::setfill('0') << value;
    return out.str();
}

std::string q(const std::string& s) { return '"' + s + '"'; }

std::string dateOf(std::int64_t hour) { return formatHour(hour).substr(0, 10); }
std::string timeOf(std::int64_t hour) { return formatHour(hour).substr(11, 2) + ":00"; }

std::string csvLine(const TestMonitor& m, std::int64_t hour, int scaled, const std::string& qualifier) {
    const std::int64_t gmt = hour - m.utcOffset;
    const bool ozone = m.pollutant == Pollutant::Ozone;
    std::ostringstream out;
    out << q(pad(m.state, 2)) << ',' << q(pad(m.county, 3)) << ',' << q(pad(m.site, 4)) << ','
        << q(std::to_string(parameterCode(m.pollutant))) << ',' << m.poc << ',' << m.lat << ',' << m.lon << ','
        << q("WGS84") << ',' << q(ozone ? "Ozone" : "Nitrogen dioxide (NO2)") << ',' << q(dateOf(hour)) << ','
        << q(timeOf(hour)) << ',' << q(dateOf(gmt)) << ',' << q(timeOf(gmt)) << ','
        << formatScaled(scaled, valueDecimals(m.pollutant)) << ','
        << q(ozone ? "Parts per million" : "Parts per billion") << ',' << (ozone ? "0.005" : "0.1") << ','
        << q("") << ',' << q(qualifier) << ',' << q("FEM") << ',' << q(ozone ? "087" : "074") << ','
        << q(ozone ? "INSTRUMENTAL - ULTRA VIOLET ABSORPTION"
                   : "Instrumental - Chemiluminescence Thermo Electron 42C-TL, 42i-TL")
        << ',' << q(m.stateName) << ',' << q(m.countyName) << ',' << q("2024-07-19");
    return out.str();
}

const char* kHeader =
    R"("State Code","County Code","Site Num","Parameter Code","POC","Latitude","Longitude","Datum",)"
    R"("Parameter Name","Date Local","Time Local","Date GMT","Time GMT","Sample Measurement",)"
    R"("Units of Measure","MDL","Uncertainty","Qualifier","Method Type","Method Code","Method Name",)"
    R"("State Name","County Name","Date of Last Change")";

int valueFor(std::size_t monitor, std::int64_t i) {
    if (kMonitors[monitor].pollutant == Pollutant::Ozone) return static_cast<int>((i * 37 + monitor * 11) % 120) - 5;
    return static_cast<int>((i * 53) % 1500) - 49;
}

// Writes one file: each listed monitor gets `hours` consecutive readings from `start`.
void writeFile(const fs::path& path, const std::vector<std::size_t>& monitors, std::int64_t start, int hours,
               std::vector<Expected>& expected, bool addBadRows) {
    std::ofstream out(path);
    out << kHeader << '\n';
    for (const std::size_t m : monitors) {
        for (int i = 0; i < hours; ++i) {
            const std::int64_t hour = start + i;
            const int value = valueFor(m, hour);
            out << csvLine(kMonitors[m], hour, value, hour % 17 == 0 ? "SX" : "") << '\n';
            expected.push_back({m, hour, value});
        }
    }
    if (addBadRows) {
        out << "\"06\",\"037\",\"1103\",\"44201\",1\n";  // too few fields
        std::string badValue = csvLine(kMonitors[0], start, 1, "");
        badValue.replace(badValue.find(",0.001,"), 7, ",abc,");  // unparsable reading
        out << badValue << '\n';
    }
}

struct Fixture {
    fs::path dir;
    std::vector<Expected> rows;
};

Fixture makeFixture() {
    Fixture fx;
    fx.dir = fs::temp_directory_path() / ("mini1_test_queries_" + std::to_string(::getpid()));
    fs::remove_all(fx.dir);
    fs::create_directories(fx.dir / "ozone");
    fs::create_directories(fx.dir / "no2");
    const std::int64_t y2021 = hoursSinceEpoch(2021, 12, 30, 0);
    const std::int64_t y2022 = hoursSinceEpoch(2022, 1, 1, 0);
    writeFile(fx.dir / "ozone" / "hourly_44201_2021.csv", {0, 1, 2}, y2021, 48, fx.rows, true);
    writeFile(fx.dir / "ozone" / "hourly_44201_2022.csv", {0, 2}, y2022, 30, fx.rows, false);
    writeFile(fx.dir / "no2" / "hourly_42602_2021.csv", {3}, y2021, 48, fx.rows, false);
    std::ofstream(fx.dir / "notes.txt") << "not a data file\n";  // must be ignored
    return fx;
}

MonitorId idOf(const AirQualityDB& db, std::size_t m) {
    const TestMonitor& t = kMonitors[m];
    MonitorKey key;
    key.state = static_cast<std::uint8_t>(t.state);
    key.county = static_cast<std::uint16_t>(t.county);
    key.site = static_cast<std::uint16_t>(t.site);
    key.pollutant = t.pollutant;
    key.poc = static_cast<std::uint8_t>(t.poc);
    const auto id = db.findMonitor(key);
    CHECK(id.has_value());
    return id.value_or(0);
}

class CollectSink final : public ResultSink {
public:
    void onBatch(std::span<const Measurement> rows) override { all.insert(all.end(), rows.begin(), rows.end()); }
    std::vector<Measurement> all;
};

void checkDatabase(const AirQualityDB& db, const LoadReport& report, const Fixture& fx) {
    CHECK_EQ(report.files.size(), 3u);
    CHECK_EQ(report.rows, fx.rows.size());
    CHECK_EQ(report.badRows, 2u);
    CHECK_EQ(report.badFieldCount, 1u);
    CHECK_EQ(report.badNumber, 1u);
    CHECK_EQ(report.uncertaintyNonEmpty, 0u);
    CHECK_EQ(report.utcOffsetChanges, 0u);
    CHECK_EQ(db.orderViolations(), 0u);
    CHECK_EQ(db.monitorCount(), 4u);
    CHECK_EQ(db.qualifierCount(), 2u);  // "" and "SX"

    const Monitor& la = db.monitor(idOf(db, 0));
    CHECK_EQ(la.stateName, std::string("California"));
    CHECK_EQ(int{la.utcOffsetHours}, -8);
    CHECK(la.latitude > 34.06 && la.latitude < 34.07);
    CHECK_EQ(db.monitor(idOf(db, 2)).utcOffsetHours, -5);

    // Q1: monitor 0 across the year boundary (rows come from two files).
    const HourRange q1{static_cast<HourIndex>(hoursSinceEpoch(2021, 12, 31, 6)),
                       static_cast<HourIndex>(hoursSinceEpoch(2022, 1, 1, 12))};
    std::vector<std::int64_t> expectedHours;
    for (const Expected& e : fx.rows) {
        if (e.monitor == 0 && q1.contains(static_cast<HourIndex>(e.hour))) expectedHours.push_back(e.hour);
    }
    const auto copy = db.rangeByMonitor(idOf(db, 0), q1);
    CHECK_EQ(copy.size(), expectedHours.size());
    for (std::size_t i = 0; i < copy.size() && i < expectedHours.size(); ++i) {
        CHECK_EQ(static_cast<std::int64_t>(copy[i].hour), expectedHours[i]);
    }
    const auto views = db.rangeByMonitorView(idOf(db, 0), q1);
    CHECK_EQ(views.size(), 2u);
    std::size_t viewed = 0;
    for (const auto& v : views) viewed += v.size();
    CHECK_EQ(viewed, expectedHours.size());
    CHECK_EQ(db.rangeByMonitorScan(idOf(db, 0), q1), expectedHours.size());
    CHECK(db.rangeByMonitor(idOf(db, 0), {0, 1}).empty());

    // Q2: ozone 0.050..0.100 ppm over all time, then within a window.
    for (const HourRange range : {HourRange{}, q1}) {
        ValueQuery query;
        query.pollutant = Pollutant::Ozone;
        query.lo = 50;
        query.hi = 100;
        query.range = range;
        std::size_t expected = 0;
        std::int64_t expectedSum = 0;
        for (const Expected& e : fx.rows) {
            if (kMonitors[e.monitor].pollutant == Pollutant::Ozone && e.value >= 50 && e.value <= 100 &&
                range.contains(static_cast<HourIndex>(e.hour))) {
                ++expected;
                expectedSum += e.value;
            }
        }
        CHECK(expected > 0);
        CHECK_EQ(db.countByValue(query), expected);
        CHECK_EQ(db.countByValueVirtual(query), expected);
        const auto selected = db.selectByValue(query);
        CHECK_EQ(selected.size(), expected);
        std::int64_t sum = 0;
        for (const auto& r : selected) sum += r.value;
        CHECK_EQ(sum, expectedSum);
        CollectSink sink;
        db.forEachByValue(query, sink);
        CHECK_EQ(sink.all.size(), expected);
    }

    // Q3: every grouping against brute force, ozone in 2021 only.
    AggregateQuery agg;
    agg.pollutant = Pollutant::Ozone;
    agg.range = {0, static_cast<HourIndex>(hoursSinceEpoch(2022, 1, 1, 0))};
    Stats total;
    std::vector<Stats> byHour(24);
    std::vector<Stats> byMonitor(db.monitorCount());
    for (const Expected& e : fx.rows) {
        if (kMonitors[e.monitor].pollutant != Pollutant::Ozone || !agg.range.contains(static_cast<HourIndex>(e.hour))) {
            continue;
        }
        const auto v = static_cast<ScaledValue>(e.value);
        total.add(v);
        byHour[static_cast<std::size_t>(e.hour % 24)].add(v);
        byMonitor[idOf(db, e.monitor)].add(v);
    }
    const auto sameStats = [](const Stats& a, const Stats& b) {
        return a.count == b.count && a.sum == b.sum && (a.count == 0 || (a.min == b.min && a.max == b.max));
    };
    agg.groupBy = GroupBy::None;
    const auto none = db.aggregate(agg);
    CHECK_EQ(none.groups.size(), 1u);
    CHECK(sameStats(none.groups[0], total));
    CHECK_EQ(none.groups[0].count, 3u * 48u);
    agg.groupBy = GroupBy::HourOfDay;
    const auto hours = db.aggregate(agg);
    CHECK_EQ(hours.groups.size(), 24u);
    for (std::size_t h = 0; h < 24; ++h) CHECK(sameStats(hours.groups[h], byHour[h]));
    agg.groupBy = GroupBy::Monitor;
    const auto monitors = db.aggregate(agg);
    CHECK_EQ(monitors.groups.size(), db.monitorCount());
    for (std::size_t m = 0; m < byMonitor.size(); ++m) CHECK(sameStats(monitors.groups[m], byMonitor[m]));

    // Q4.
    CHECK_EQ(db.monitorsInState(6).size(), 3u);
    CHECK_EQ(db.monitorsInState(6, Pollutant::Ozone).size(), 2u);
    CHECK_EQ(db.monitorsInState(36).size(), 1u);
    CHECK(db.monitorsInState(1).empty());
    CHECK_EQ(db.monitorsInBox(33.0, 35.0, -119.0, -117.0).size(), 3u);
    CHECK_EQ(db.monitorsInBox(33.0, 35.0, -119.0, -117.0, Pollutant::NO2).size(), 1u);

    // Reading values survive the round trip, including negatives.
    CHECK_EQ(db.rowCount(Pollutant::NO2), 48u);
    CHECK_EQ(db.rowCount(Pollutant::Ozone), fx.rows.size() - 48u);
}

}  // namespace

int main() {
    const Fixture fx = makeFixture();

    for (const LoaderKind kind : {LoaderKind::Naive, LoaderKind::Getline, LoaderKind::Buffered}) {
        AirQualityDB db;
        LoadOptions options;
        options.loader = kind;
        checkDatabase(db, db.load(fx.dir, options), fx);
    }

    // A tiny buffer forces lines to be split across reads.
    {
        AirQualityDB db;
        LoadOptions options;
        options.chunkBytes = 64;
        options.reserve = false;
        checkDatabase(db, db.load(fx.dir, options), fx);
    }

    // Filters: one pollutant, one year.
    {
        AirQualityDB db;
        LoadOptions options;
        options.includeNo2 = false;
        options.yearFrom = 2022;
        const LoadReport report = db.load(fx.dir, options);
        CHECK_EQ(report.files.size(), 1u);
        CHECK_EQ(report.rows, 2u * 30u);
    }

    fs::remove_all(fx.dir);
    return testutil::finish("test_queries");
}
