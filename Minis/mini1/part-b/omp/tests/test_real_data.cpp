// Checks against the real dataset, using facts verified independently
// (Python passes over the raw CSVs). Uses only the smallest file so it runs in
// about a second. Skips when the dataset is not present.
//
// argv[1] = dataset directory (Minis/Dataset)

#include "AirQualityDB.hpp"
#include "CsvTokenizer.hpp"
#include "Parse.hpp"
#include "TestUtil.hpp"
#include "Time.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>

using namespace aq;
namespace fs = std::filesystem;

int main(int argc, char** argv) {
    const fs::path data = argc > 1 ? fs::path(argv[1]) : fs::path("../../Dataset");
    const fs::path no2_2026 = data / "no2" / "hourly_42602_2026.csv";
    if (!fs::exists(no2_2026)) {
        std::cout << "test_real_data: SKIPPED (" << no2_2026 << " not found; run scripts/get_data.sh)\n";
        return 0;
    }

    AirQualityDB db;
    const LoadReport report = db.load(no2_2026);
    CHECK_EQ(report.rows, 805'803u);  // wc -l minus the header
    CHECK_EQ(report.badRows, 0u);
    CHECK_EQ(report.uncertaintyNonEmpty, 0u);
    CHECK_EQ(db.orderViolations(), 0u);

    HourIndex first = std::numeric_limits<HourIndex>::max();
    HourIndex last = 0;
    for (std::size_t i = 0; i < db.monitorCount(); ++i) {
        first = std::min(first, db.monitor(static_cast<MonitorId>(i)).firstHour);
        last = std::max(last, db.monitor(static_cast<MonitorId>(i)).lastHour);
    }
    CHECK_EQ(formatHour(first).substr(0, 10), std::string("2026-01-01"));
    CHECK_EQ(formatHour(last).substr(0, 10), std::string("2026-05-31"));

    // First data row of this file: "01","073","0023","42602",1,... "2026-01-01","00:00" ... 29.5
    const auto id = db.findMonitor(parseMonitorKey("01-073-0023-42602-1").value());
    CHECK(id.has_value());
    if (id) {
        const auto rows = db.rangeByMonitor(*id, {0, parseHourArg("2026-01-01T01").value()});
        CHECK_EQ(rows.size(), 1u);
        if (!rows.empty()) CHECK_EQ(rows[0].value, 295);
        CHECK_EQ(db.monitor(*id).stateName, std::string("Alabama"));
    }

    // First data row of the 2024 ozone file, parsed field by field.
    const fs::path ozone2024 = data / "ozone" / "hourly_44201_2024.csv";
    if (fs::exists(ozone2024)) {
        std::ifstream in(ozone2024);
        std::string header, line;
        std::getline(in, header);
        std::getline(in, line);
        CsvTokenizer::Fields f;
        CHECK_EQ(CsvTokenizer::split(header, f), 24u);
        CHECK_EQ(CsvTokenizer::split(line, f), 24u);
        CHECK_EQ(f[0], std::string_view("01"));
        CHECK_EQ(f[1], std::string_view("003"));
        CHECK_EQ(f[2], std::string_view("0010"));
        CHECK_EQ(f[9], std::string_view("2024-03-01"));
        CHECK_EQ(f[10], std::string_view("01:00"));
        std::int32_t value = 0;
        CHECK(parse::scaled(f[13], 3, value));
        CHECK_EQ(value, 62);
    }

    return testutil::finish("test_real_data");
}
