#pragma once

// Reads EPA AQS hourly CSV files and feeds rows into the registry,
// dictionaries and store. Three reader versions are kept so they can be
// benchmarked against each other:
//
//   naive    - std::getline, one std::string per field, std::stoi / std::stod
//   getline  - std::getline, string_view fields, std::from_chars, exact decimals
//   buffered - large fread() chunks (default 8 MB), otherwise like getline

#include "Dictionary.hpp"
#include "aqdata/Load.hpp"
#include "MeasurementStore.hpp"
#include "MonitorRegistry.hpp"
#include "aqdata/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace aq {

// One EPA file: hourly_<parameter code>_<year>.csv
struct DataFile {
    std::filesystem::path path;
    Pollutant pollutant = Pollutant::Ozone;
    int year = 0;
    std::uintmax_t bytes = 0;
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
