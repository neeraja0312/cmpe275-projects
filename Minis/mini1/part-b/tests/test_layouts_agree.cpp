// Loads the 2024 files into both storage layouts and checks that every query,
// in every result mode, gives exactly the same answer. This is the correctness
// guard for the column layout: it may change speed and memory, never results.
//
// argv[1] = dataset directory (Minis/Dataset). Skipped if no 2024 data is there.

#include "TestUtil.hpp"
#include "aqdata/Database.hpp"
#include "aqdata/Time.hpp"

#include <filesystem>
#include <iostream>
#include <limits>
#include <vector>

using namespace aq;
namespace fs = std::filesystem;

namespace {

class CollectSink final : public ResultSink {
public:
    void onBatch(std::span<const Measurement> rows) override {
        ++batches;
        all.insert(all.end(), rows.begin(), rows.end());
    }
    std::size_t batches = 0;
    std::vector<Measurement> all;
};

bool sameRow(const Measurement& a, const Measurement& b) {
    return a.hour == b.hour && a.monitor == b.monitor && a.value == b.value && a.qualifier == b.qualifier &&
           a.method == b.method;
}

bool sameRows(const std::vector<Measurement>& a, const std::vector<Measurement>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!sameRow(a[i], b[i])) return false;
    }
    return true;
}

bool sameStats(const Stats& a, const Stats& b) {
    return a.count == b.count && a.sum == b.sum && (a.count == 0 || (a.min == b.min && a.max == b.max));
}

}  // namespace

int main(int argc, char** argv) {
    const fs::path data = argc > 1 ? fs::path(argv[1]) : fs::path("../../Dataset");
    if (!fs::exists(data / "ozone" / "hourly_44201_2024.csv")) {
        std::cout << "test_layouts_agree: SKIPPED (no 2024 data under " << data << ")\n";
        return 0;
    }

    LoadOptions options;
    options.yearFrom = 2024;
    options.yearTo = 2024;
    Database aos(Layout::Aos);
    Database columns(Layout::Columns);
    const LoadReport ra = aos.load(data, options);
    const LoadReport rc = columns.load(data, options);

    CHECK_EQ(ra.rows, rc.rows);
    CHECK_EQ(ra.badRows, 0u);
    CHECK_EQ(rc.badRows, 0u);
    CHECK_EQ(aos.rowCount(), columns.rowCount());
    CHECK_EQ(aos.monitorCount(), columns.monitorCount());
    CHECK_EQ(aos.orderViolations(), columns.orderViolations());
    CHECK(columns.storeBytes() < aos.storeBytes());  // 10 bytes per row against 12
    CHECK(std::string(aos.storeName()) == "aos");
    CHECK(std::string(columns.storeName()) == "columns");

    const HourRange y2024{parseHourArg("2024-01-01").value(), parseHourArg("2025-01-01").value()};
    const HourRange july4{parseHourArg("2024-07-04T12").value(), parseHourArg("2024-07-04T13").value()};
    const HourRange summer{parseHourArg("2024-06-15").value(), parseHourArg("2024-06-20").value()};
    constexpr ScaledValue kMax = std::numeric_limits<ScaledValue>::max();
    constexpr ScaledValue kMin = std::numeric_limits<ScaledValue>::min();

    // ---- Q2: all four result modes, partial and full ranges, both pollutants.
    const ValueQuery queries[] = {
        {Pollutant::Ozone, 71, kMax, {}},          // > 0.070 ppm, every year loaded
        {Pollutant::Ozone, 71, kMax, summer},      // partial time range
        {Pollutant::Ozone, kMin, kMax, july4},     // time-only search
        {Pollutant::Ozone, 30, 60, y2024},
        {Pollutant::NO2, 1001, kMax, y2024},       // > 100.0 ppb
        {Pollutant::NO2, 0, 200, {}},
    };
    for (const ValueQuery& q : queries) {
        const std::size_t count = aos.countByValue(q);
        CHECK_EQ(columns.countByValue(q), count);
        CHECK_EQ(aos.countByValueVirtual(q), count);
        CHECK_EQ(columns.countByValueVirtual(q), count);

        const auto rowsA = aos.selectByValue(q);
        const auto rowsC = columns.selectByValue(q);
        CHECK_EQ(rowsA.size(), count);
        CHECK(sameRows(rowsA, rowsC));  // same rows, same order

        CollectSink sinkA, sinkC;
        aos.forEachByValue(q, sinkA);
        columns.forEachByValue(q, sinkC);
        CHECK(sameRows(sinkA.all, rowsA));
        CHECK(sameRows(sinkC.all, rowsA));
        CHECK_EQ(sinkA.batches, sinkC.batches);
    }

    // ---- Q3: every grouping, full statistics.
    for (const Pollutant p : {Pollutant::Ozone, Pollutant::NO2}) {
        for (const HourRange range : {y2024, summer}) {
            for (const GroupBy g : {GroupBy::None, GroupBy::Monitor, GroupBy::HourOfDay}) {
                const AggregateQuery q{p, range, g};
                const AggregateResult a = aos.aggregate(q);
                const AggregateResult c = columns.aggregate(q);
                CHECK_EQ(a.groups.size(), c.groups.size());
                bool same = a.groups.size() == c.groups.size();
                for (std::size_t i = 0; same && i < a.groups.size(); ++i) same = sameStats(a.groups[i], c.groups[i]);
                CHECK(same);
            }
        }
    }

    // ---- Q1: every 25th monitor, three ranges, copy / view / scan.
    std::size_t checkedMonitors = 0;
    for (MonitorId id = 0; id < aos.monitorCount(); id += 25) {
        ++checkedMonitors;
        for (const HourRange range : {y2024, summer, july4}) {
            const auto copyA = aos.rangeByMonitor(id, range);
            const auto copyC = columns.rangeByMonitor(id, range);
            CHECK(sameRows(copyA, copyC));
            CHECK_EQ(aos.rangeByMonitorScan(id, range), copyA.size());
            CHECK_EQ(columns.rangeByMonitorScan(id, range), copyA.size());

            // Views from both layouts read the same rows.
            const auto viewsA = aos.rangeByMonitorView(id, range);
            const auto viewsC = columns.rangeByMonitorView(id, range);
            std::vector<Measurement> fromA, fromC;
            for (const auto& v : viewsA) {
                for (std::size_t i = 0; i < v.size; ++i) fromA.push_back(v.row(i));
            }
            for (const auto& v : viewsC) {
                for (std::size_t i = 0; i < v.size; ++i) fromC.push_back(v.row(i));
            }
            CHECK(sameRows(fromA, copyA));
            CHECK(sameRows(fromC, copyA));
        }
    }
    CHECK(checkedMonitors > 40);

    return testutil::finish("test_layouts_agree");
}
