// CSV tokenizer on real EPA header and row formats.

#include "CsvTokenizer.hpp"
#include "TestUtil.hpp"

#include <string_view>

using aq::CsvTokenizer;

int main() {
    CsvTokenizer::Fields f;

    constexpr std::string_view header =
        R"("State Code","County Code","Site Num","Parameter Code","POC","Latitude","Longitude","Datum",)"
        R"("Parameter Name","Date Local","Time Local","Date GMT","Time GMT","Sample Measurement",)"
        R"("Units of Measure","MDL","Uncertainty","Qualifier","Method Type","Method Code","Method Name",)"
        R"("State Name","County Name","Date of Last Change")";
    CHECK_EQ(CsvTokenizer::split(header, f), 24u);
    CHECK_EQ(f[0], std::string_view("State Code"));
    CHECK_EQ(f[23], std::string_view("Date of Last Change"));

    // Real row: unquoted numbers, empty quoted fields.
    constexpr std::string_view row =
        R"("01","003","0010","44201",1,30.497478,-87.880258,"NAD83","Ozone","2024-03-01","01:00",)"
        R"("2024-03-01","07:00",0.062,"Parts per million",0.005,"","","FEM","087",)"
        R"("INSTRUMENTAL - ULTRA VIOLET ABSORPTION","Alabama","Baldwin","2024-07-19")";
    CHECK_EQ(CsvTokenizer::split(row, f), 24u);
    CHECK_EQ(f[0], std::string_view("01"));
    CHECK_EQ(f[4], std::string_view("1"));
    CHECK_EQ(f[6], std::string_view("-87.880258"));
    CHECK_EQ(f[13], std::string_view("0.062"));
    CHECK(f[16].empty());
    CHECK(f[17].empty());
    CHECK_EQ(f[22], std::string_view("Baldwin"));

    // A method name containing a comma must stay one field.
    constexpr std::string_view comma =
        R"x("06","037","1103","42602",1,34.06659,-118.22688,"WGS84","Nitrogen dioxide (NO2)","2024-01-01",)x"
        R"x("00:00","2024-01-01","08:00",21.3,"Parts per billion",0.1,"","","FEM","074",)x"
        R"x("Instrumental - Chemiluminescence Thermo Electron 42C-TL, 42i-TL","California","Los Angeles","2024-04-01")x";
    CHECK_EQ(CsvTokenizer::split(comma, f), 24u);
    CHECK_EQ(f[20], std::string_view("Instrumental - Chemiluminescence Thermo Electron 42C-TL, 42i-TL"));
    CHECK_EQ(f[21], std::string_view("California"));

    // Windows line ending, trailing empty field, empty line.
    CHECK_EQ(CsvTokenizer::split("\"a\",\"b\"\r", f), 2u);
    CHECK_EQ(f[1], std::string_view("b"));
    CHECK_EQ(CsvTokenizer::split("a,b,", f), 3u);
    CHECK(f[2].empty());
    CHECK_EQ(CsvTokenizer::split("", f), 1u);

    // Too many fields is reported, not overflowed.
    std::string_view many = ",,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,";  // 41 fields
    CHECK_EQ(CsvTokenizer::split(many, f), CsvTokenizer::kMaxFields + 1);

    // Unterminated quote takes the rest of the line instead of reading past it.
    CHECK_EQ(CsvTokenizer::split("x,\"abc", f), 2u);
    CHECK_EQ(f[1], std::string_view("abc"));

    return testutil::finish("test_tokenizer");
}
