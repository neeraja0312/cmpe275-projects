#pragma once

// Reads EPA AQS hourly CSV files and feeds rows into the registry,
// dictionaries and store. Three reader versions are kept so they can be
// benchmarked against each other:
//
//   naive    - std::getline, one std::string per field, std::stoi / std::stod
//   getline  - std::getline, string_view fields, std::from_chars, exact decimals
//   buffered - large fread() chunks (default 8 MB), otherwise like getline

#include "Dictionary.hpp"
#include "MeasurementStore.hpp"
#include "MonitorRegistry.hpp"
#include "Types.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace aq {

enum class LoaderKind : std::uint8_t { Naive, Getline, Buffered };

std::optional<LoaderKind> loaderKindFromName(std::string_view name);
std::string_view loaderKindName(LoaderKind kind);

struct LoadOptions {
    LoaderKind loader = LoaderKind::Buffered;
    bool reserve = true;                   // pre-size storage from total file size
    std::size_t chunkBytes = std::size_t{8} * 1024 * 1024;  // buffered reader chunk size
    bool includeOzone = true;
    bool includeNo2 = true;
    int yearFrom = 0;
    int yearTo = 9999;
};

// One EPA file: hourly_<parameter code>_<year>.csv
struct DataFile {
    std::filesystem::path path;
    Pollutant pollutant = Pollutant::Ozone;
    int year = 0;
    std::uintmax_t bytes = 0;
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

// Average CSV bytes per row is ~236; dividing by 230 over-reserves slightly so
// the row vector never has to grow (growth would double peak memory).
inline constexpr std::size_t kReserveBytesPerRow = 230;

class DataLoader {
public:
    DataLoader(MonitorRegistry& registry, Dictionary<CodeId>& qualifiers, Dictionary<CodeId>& methods,
               MeasurementStore& store);

    // Finds hourly_<code>_<year>.csv files under a directory (recursively), or
    // accepts a single such file. Sorted by pollutant, then year, so each
    // monitor's rows arrive in time order.
    static std::vector<DataFile> discover(const std::filesystem::path& path, const LoadOptions& options);

    // Parses "hourly_44201_2024.csv" -> (Ozone, 2024).
    static std::optional<DataFile> describe(const std::filesystem::path& path);

    LoadReport load(const std::filesystem::path& path, const LoadOptions& options);

private:
    MonitorRegistry& registry_;
    Dictionary<CodeId>& qualifiers_;
    Dictionary<CodeId>& methods_;
    MeasurementStore& store_;
};

}  // namespace aq
