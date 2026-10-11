#pragma once

// Options and report for loading EPA AQS hourly CSV files. Three reader
// versions are kept so they can be benchmarked against each other:
//
//   naive    - std::getline, one std::string per field, std::stoi / std::stod
//   getline  - std::getline, string_view fields, std::from_chars, exact decimals
//   buffered - large fread() chunks (default 8 MB), otherwise like getline

#include "aqdata/Export.hpp"
#include "aqdata/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace aq {

enum class LoaderKind : std::uint8_t { Naive, Getline, Buffered };

AQ_API std::optional<LoaderKind> loaderKindFromName(std::string_view name);
AQ_API std::string_view loaderKindName(LoaderKind kind);

struct LoadOptions {
    LoaderKind loader = LoaderKind::Buffered;
    bool reserve = true;                   // pre-size storage from total file size
    std::size_t chunkBytes = std::size_t{8} * 1024 * 1024;  // buffered reader chunk size
    bool includeOzone = true;
    bool includeNo2 = true;
    int yearFrom = 0;
    int yearTo = 9999;
};

struct FileReport {
    std::filesystem::path path;
    Pollutant pollutant = Pollutant::Ozone;
    int year = 0;
    std::uint64_t bytes = 0;
    std::uint64_t rows = 0;
    std::uint64_t badRows = 0;
    double seconds = 0.0;
};

struct LoadReport {
    std::vector<FileReport> files;
    std::uint64_t bytes = 0;
    std::uint64_t rows = 0;
    std::uint64_t badRows = 0;
    // Why rows were rejected.
    std::uint64_t badFieldCount = 0;
    std::uint64_t badNumber = 0;
    std::uint64_t badPollutant = 0;
    std::uint64_t badDateTime = 0;
    std::uint64_t valueOutOfRange = 0;
    // Data checks (expected to be 0 for this dataset).
    std::uint64_t uncertaintyNonEmpty = 0;  // EPA "Uncertainty" column is always empty
    std::uint64_t utcOffsetChanges = 0;     // rows whose local-GMT offset differs from their monitor's
    std::size_t reservedRows = 0;
    double seconds = 0.0;
};

}  // namespace aq
