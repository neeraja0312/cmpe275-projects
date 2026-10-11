// Equivalence and edge-case checks for the single-pass tokenizer used by the
// private library loader.

#include "CsvTokenizer.hpp"
#include "TestUtil.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

using aq::CsvTokenizer;
namespace fs = std::filesystem;

namespace {

// The previous implementation is retained only as a test oracle so real EPA
// rows can prove that the optimized parser did not change field boundaries.
std::size_t splitReference(std::string_view line, CsvTokenizer::Fields& out) {
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

    std::size_t count = 0;
    std::size_t pos = 0;
    while (true) {
        if (count == CsvTokenizer::kMaxFields) return CsvTokenizer::kMaxFields + 1;

        std::size_t afterField;
        if (pos < line.size() && line[pos] == '"') {
            const std::size_t close = line.find('"', pos + 1);
            afterField = close == std::string_view::npos ? line.size() : close + 1;
            const std::size_t end = close == std::string_view::npos ? line.size() : close;
            out[count++] = std::string_view(line.data() + pos + 1, end - pos - 1);
        } else {
            const std::size_t comma = line.find(',', pos);
            afterField = comma == std::string_view::npos ? line.size() : comma;
            out[count++] = std::string_view(line.data() + pos, afterField - pos);
        }

        const std::size_t comma = line.find(',', afterField);
        if (comma == std::string_view::npos) break;
        pos = comma + 1;
    }
    return count;
}

bool sameFields(std::string_view line) {
    CsvTokenizer::Fields actual;
    CsvTokenizer::Fields expected;
    const std::size_t actualCount = CsvTokenizer::split(line, actual);
    const std::size_t expectedCount = splitReference(line, expected);
    if (actualCount != expectedCount) return false;
    const std::size_t comparable = std::min(actualCount, CsvTokenizer::kMaxFields);
    for (std::size_t i = 0; i < comparable; ++i) {
        if (actual[i] != expected[i]) return false;
    }
    return true;
}

bool checkFile(const fs::path& path) {
    std::ifstream input(path);
    if (!input) return false;
    std::string line;
    while (std::getline(input, line)) {
        if (!sameFields(line)) return false;
    }
    return !input.bad();
}

}  // namespace

int main(int argc, char** argv) {
    CsvTokenizer::Fields fields;

    constexpr std::string_view header =
        R"("State Code","County Code","Site Num","Parameter Code","POC","Latitude","Longitude","Datum",)"
        R"("Parameter Name","Date Local","Time Local","Date GMT","Time GMT","Sample Measurement",)"
        R"("Units of Measure","MDL","Uncertainty","Qualifier","Method Type","Method Code","Method Name",)"
        R"("State Name","County Name","Date of Last Change")";
    CHECK_EQ(CsvTokenizer::split(header, fields), 24u);
    CHECK_EQ(fields[0], std::string_view("State Code"));
    CHECK_EQ(fields[23], std::string_view("Date of Last Change"));

    constexpr std::string_view row =
        R"x("06","037","1103","42602",1,34.06659,-118.22688,"WGS84","Nitrogen dioxide (NO2)","2024-01-01",)x"
        R"x("00:00","2024-01-01","08:00",21.3,"Parts per billion",0.1,"","","FEM","074",)x"
        R"x("Instrumental - Chemiluminescence Thermo Electron 42C-TL, 42i-TL","California","Los Angeles","2024-04-01")x";
    CHECK_EQ(CsvTokenizer::split(row, fields), 24u);
    CHECK_EQ(fields[4], std::string_view("1"));
    CHECK_EQ(fields[16], std::string_view(""));
    CHECK_EQ(fields[20], std::string_view("Instrumental - Chemiluminescence Thermo Electron 42C-TL, 42i-TL"));
    CHECK_EQ(fields[21], std::string_view("California"));

    CHECK_EQ(CsvTokenizer::split("\"a\",\"b\"\r", fields), 2u);
    CHECK_EQ(fields[1], std::string_view("b"));
    CHECK_EQ(CsvTokenizer::split("a,b,", fields), 3u);
    CHECK(fields[2].empty());
    CHECK_EQ(CsvTokenizer::split("", fields), 1u);
    CHECK_EQ(CsvTokenizer::split("\r", fields), 1u);
    CHECK(fields[0].empty());
    CHECK_EQ(CsvTokenizer::split("\"\"", fields), 1u);
    CHECK(fields[0].empty());
    CHECK_EQ(CsvTokenizer::split("\"a,b\",c", fields), 2u);
    CHECK_EQ(fields[0], std::string_view("a,b"));
    CHECK_EQ(fields[1], std::string_view("c"));
    CHECK_EQ(CsvTokenizer::split("a\"b,c", fields), 2u);
    CHECK_EQ(fields[0], std::string_view("a\"b"));

    constexpr std::string_view many = ",,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,";
    CHECK_EQ(CsvTokenizer::split(many, fields), CsvTokenizer::kMaxFields + 1);

    CHECK_EQ(CsvTokenizer::split("x,\"abc", fields), 2u);
    CHECK_EQ(fields[1], std::string_view("abc"));

    const fs::path data = argc > 1 ? fs::path(argv[1]) : fs::path("../../Dataset");
    bool checkedDataset = false;
    for (const auto& path : {data / "ozone" / "hourly_44201_2024.csv",
                             data / "no2" / "hourly_42602_2024.csv"}) {
        if (!fs::exists(path)) continue;
        checkedDataset = true;
        CHECK(checkFile(path));
    }
    if (!checkedDataset) {
        std::cout << "test_tokenizer: dataset parity SKIPPED (no 2024 data under " << data << ")\n";
    }

    return testutil::finish("test_tokenizer");
}
