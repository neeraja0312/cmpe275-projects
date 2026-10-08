// Loads all 12 files and checks totals and 2024 facts that were verified
// independently with Python over the raw CSVs. Takes about a minute and
// ~1 GB of RAM, so it only runs when asked:
//
//   MINI1_FULL_TEST=1 ctest --test-dir part-a/build -R full --output-on-failure
//
// argv[1] = dataset directory (Minis/Dataset)

#include "AirQualityDB.hpp"
#include "TestUtil.hpp"
#include "Time.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>

using namespace aq;
namespace fs = std::filesystem;

int main(int argc, char** argv) {
    const char* enabled = std::getenv("MINI1_FULL_TEST");
    if (enabled == nullptr || std::string(enabled) != "1") {
        std::cout << "test_full_dataset: SKIPPED (set MINI1_FULL_TEST=1 to run)\n";
        return 0;
    }
    const fs::path data = argc > 1 ? fs::path(argv[1]) : fs::path("../../Dataset");

    AirQualityDB db;
    const LoadReport report = db.load(data);
    std::cout << "loaded " << report.rows << " rows in " << report.seconds << " s\n";

    CHECK_EQ(report.files.size(), 12u);
    CHECK_EQ(report.badRows, 0u);
    CHECK_EQ(report.rows, 65'742'181u);
    CHECK_EQ(db.rowCount(Pollutant::Ozone), 47'005'021u);
    CHECK_EQ(db.rowCount(Pollutant::NO2), 18'737'160u);
    CHECK_EQ(report.uncertaintyNonEmpty, 0u);
    CHECK_EQ(db.orderViolations(), 0u);

    const HourRange y2024{parseHourArg("2024-01-01").value(), parseHourArg("2025-01-01").value()};

    // Rows, monitors and extremes in 2024.
    struct Facts {
        Pollutant pollutant;
        std::uint64_t rows;
        std::size_t monitors;
        ScaledValue min, max;
    };
    for (const Facts& facts : {Facts{Pollutant::Ozone, 8'995'978, 1'283, -5, 379},
                               Facts{Pollutant::NO2, 3'547'016, 473, -49, 1413}}) {
        AggregateQuery query{facts.pollutant, y2024, GroupBy::Monitor};
        const AggregateResult byMonitor = db.aggregate(query);
        std::size_t monitors = 0;
        Stats total;
        for (const Stats& s : byMonitor.groups) {
            if (s.count > 0) ++monitors;
            total.merge(s);
        }
        CHECK_EQ(total.count, facts.rows);
        CHECK_EQ(monitors, facts.monitors);
        CHECK_EQ(total.min, facts.min);
        CHECK_EQ(total.max, facts.max);
    }

    // Selectivity of threshold searches in 2024.
    ValueQuery no2Over100{Pollutant::NO2, 1001, std::numeric_limits<ScaledValue>::max(), y2024};  // > 100.0 ppb
    CHECK_EQ(db.countByValue(no2Over100), 3u);

    ValueQuery ozoneOver70{Pollutant::Ozone, 71, std::numeric_limits<ScaledValue>::max(), y2024};  // > 0.070 ppm
    const double share = static_cast<double>(db.countByValue(ozoneOver70)) / 8'995'978.0;
    std::cout << "ozone > 0.070 ppm in 2024: " << share * 100.0 << "%\n";
    CHECK(share > 0.0068 && share < 0.0070);

    return testutil::finish("test_full_dataset");
}
