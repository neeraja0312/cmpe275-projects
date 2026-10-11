#pragma once

// Deliberate "what if we had done it the obvious way" experiments. Their
// numbers are report material; they are not used by the library itself.

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace aq {

// Load one CSV keeping every field as a std::string (the naive row design)
// and measure what that costs per row.
struct StringRowsResult {
    std::uint64_t rows = 0;
    std::size_t bytesPerRow = 0;    // sizeof(row) + heap bytes of long strings
    std::size_t peakRssBytes = 0;
    double seconds = 0.0;
};
StringRowsResult loadAsStringRows(const std::filesystem::path& csv);

// Compare exact decimal parsing with float/double parsing followed by
// truncation or rounding to the scaled integer, and check a threshold test
// ("value > 0.070 ppm" / "value > 100.0 ppb") done on floats against a double
// literal. Counts disagreements with the exact answer.
struct FloatValuesResult {
    std::uint64_t rows = 0;
    std::uint64_t atThreshold = 0;      // rows exactly equal to the threshold
    std::uint64_t floatThreshold = 0;   // (float value > double threshold) != exact answer
    std::uint64_t doubleThreshold = 0;  // (double value > double threshold) != exact answer
    std::uint64_t floatTruncated = 0;   // (int)(strtof(s) * scale) != exact
    std::uint64_t floatRounded = 0;     // lround(strtof(s) * scale) != exact
    std::uint64_t doubleTruncated = 0;  // (int)(strtod(s) * scale) != exact
    std::uint64_t doubleRounded = 0;    // lround(strtod(s) * scale) != exact
};
FloatValuesResult compareValueParsing(const std::filesystem::path& csv);

}  // namespace aq
