#include "Experiments.hpp"

#include "CsvTokenizer.hpp"
#include "DataLoader.hpp"
#include "MemoryUsage.hpp"
#include "Parse.hpp"
#include "Timer.hpp"

#include <array>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace aq {

namespace {

constexpr std::size_t kColumns = 24;
constexpr std::size_t kSampleColumn = 13;

std::ifstream openWithoutHeader(const std::filesystem::path& csv) {
    std::ifstream in(csv);
    if (!in) throw std::runtime_error("cannot open " + csv.string());
    std::string header;
    std::getline(in, header);
    return in;
}

}  // namespace

StringRowsResult loadAsStringRows(const std::filesystem::path& csv) {
    struct StringRow {
        std::array<std::string, kColumns> fields;
    };

    const Stopwatch watch;
    std::ifstream in = openWithoutHeader(csv);
    std::vector<StringRow> rows;
    std::string line;
    CsvTokenizer::Fields fields;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const std::size_t count = CsvTokenizer::split(line, fields);
        StringRow row;
        for (std::size_t i = 0; i < kColumns && i < count; ++i) row.fields[i] = fields[i];
        rows.push_back(std::move(row));
    }

    StringRowsResult result;
    result.rows = rows.size();
    result.seconds = watch.seconds();
    result.peakRssBytes = peakRssBytes();

    // Strings longer than the small-string buffer allocate on the heap.
    const std::size_t inlineCapacity = std::string().capacity();
    std::size_t heapBytes = 0;
    for (const StringRow& row : rows) {
        for (const std::string& field : row.fields) {
            if (field.capacity() > inlineCapacity) heapBytes += field.capacity() + 1;
        }
    }
    result.bytesPerRow = sizeof(StringRow) + (rows.empty() ? 0 : heapBytes / rows.size());
    return result;
}

FloatValuesResult compareValueParsing(const std::filesystem::path& csv) {
    const auto file = DataLoader::describe(csv);
    if (!file) throw std::runtime_error(csv.string() + " is not named like an EPA hourly file");
    const int decimals = valueDecimals(file->pollutant);
    const double scale = valueScale(file->pollutant);
    const bool ozone = file->pollutant == Pollutant::Ozone;
    const double threshold = ozone ? 0.070 : 100.0;          // EPA standard levels
    const std::int32_t thresholdScaled = ozone ? 70 : 1000;

    FloatValuesResult result;
    std::ifstream in = openWithoutHeader(csv);
    std::string line;
    CsvTokenizer::Fields fields;
    while (std::getline(in, line)) {
        if (CsvTokenizer::split(line, fields) != kColumns) continue;
        const std::string sample(fields[kSampleColumn]);
        std::int32_t exact = 0;
        if (!parse::scaled(sample, decimals, exact)) continue;

        const float f = std::strtof(sample.c_str(), nullptr);
        const double d = std::strtod(sample.c_str(), nullptr);
        const float fScaled = f * static_cast<float>(scale);
        const double dScaled = d * scale;
        ++result.rows;
        if (static_cast<std::int32_t>(fScaled) != exact) ++result.floatTruncated;
        if (std::lround(fScaled) != exact) ++result.floatRounded;
        if (static_cast<std::int32_t>(dScaled) != exact) ++result.doubleTruncated;
        if (std::lround(dScaled) != exact) ++result.doubleRounded;

        const bool truth = exact > thresholdScaled;
        if (exact == thresholdScaled) ++result.atThreshold;
        if ((static_cast<double>(f) > threshold) != truth) ++result.floatThreshold;
        if ((d > threshold) != truth) ++result.doubleThreshold;
    }
    return result;
}

}  // namespace aq
