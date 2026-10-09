#include "DataLoader.hpp"

#include "CsvTokenizer.hpp"
#include "Parse.hpp"
#include "aqdata/Time.hpp"
#include "aqdata/Metrics.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>

namespace aq {

namespace fs = std::filesystem;

namespace {

// EPA hourly column positions.
enum Column : std::uint8_t {
    kState = 0, kCounty = 1, kSite = 2, kParameter = 3, kPoc = 4,
    kLatitude = 5, kLongitude = 6, kDatum = 7,
    kDateLocal = 9, kTimeLocal = 10, kDateGmt = 11, kTimeGmt = 12,
    kSample = 13, kUncertainty = 16, kQualifier = 17,
    kMethodType = 18, kMethodCode = 19, kStateName = 21, kCountyName = 22,
};
constexpr std::size_t kFieldCount = 24;

// ---- number conversion policies ---------------------------------------------

// Fast path: from_chars and exact decimal parsing, no allocation.
struct FastNumbers {
    template <typename T>
    static bool integer(std::string_view s, T& out) { return parse::unsignedInt(s, out); }
    static bool real(std::string_view s, double& out) { return parse::real(s, out); }
    static bool scaled(std::string_view s, int decimals, std::int32_t& out) {
        return parse::scaled(s, decimals, out);
    }
    static bool date(std::string_view s, int& y, unsigned& m, unsigned& d) { return parse::date(s, y, m, d); }
    static bool hour(std::string_view s, unsigned& h) { return parse::hourOfDay(s, h); }
};

// Naive path: the standard-library conversions most code reaches for first.
// Every call builds a temporary std::string; failures surface as exceptions.
struct NaiveNumbers {
    template <typename T>
    static bool integer(std::string_view s, T& out) {
        try {
            std::size_t used = 0;
            const unsigned long value = std::stoul(std::string(s), &used);
            if (used != s.size() || value > std::numeric_limits<T>::max()) return false;
            out = static_cast<T>(value);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }
    static bool real(std::string_view s, double& out) {
        try {
            std::size_t used = 0;
            out = std::stod(std::string(s), &used);
            return used == s.size();
        } catch (const std::exception&) {
            return false;
        }
    }
    static bool scaled(std::string_view s, int decimals, std::int32_t& out) {
        double value = 0.0;
        if (!real(s, value)) return false;
        out = static_cast<std::int32_t>(std::lround(value * std::pow(10.0, decimals)));
        return true;
    }
    static bool date(std::string_view s, int& y, unsigned& m, unsigned& d) {
        if (s.size() != 10) return false;
        try {
            y = std::stoi(std::string(s.substr(0, 4)));
            m = static_cast<unsigned>(std::stoi(std::string(s.substr(5, 2))));
            d = static_cast<unsigned>(std::stoi(std::string(s.substr(8, 2))));
        } catch (const std::exception&) {
            return false;
        }
        return m >= 1 && m <= 12 && d >= 1 && d <= 31;
    }
    static bool hour(std::string_view s, unsigned& h) {
        if (s.size() != 5 || s.substr(2) != ":00") return false;
        try {
            h = static_cast<unsigned>(std::stoi(std::string(s.substr(0, 2))));
        } catch (const std::exception&) {
            return false;
        }
        return h < 24;
    }
};

// ---- readers ----------------------------------------------------------------

// Calls onLine for every data line (the header line is skipped).
template <typename OnLine>
void readWithGetline(const fs::path& path, OnLine&& onLine) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path.string());
    std::string line;
    bool header = true;
    while (std::getline(in, line)) {
        if (header) {
            header = false;
            continue;
        }
        if (!line.empty()) onLine(std::string_view(line));
    }
    if (in.bad()) throw std::runtime_error("read error in " + path.string());
}

template <typename OnLine>
void readBuffered(const fs::path& path, std::size_t chunkBytes, OnLine&& onLine) {
    const std::unique_ptr<std::FILE, int (*)(std::FILE*)> file(std::fopen(path.c_str(), "rb"), &std::fclose);
    if (!file) throw std::runtime_error("cannot open " + path.string());

    std::vector<char> buffer(std::max<std::size_t>(chunkBytes, 64));
    std::size_t carried = 0;  // bytes of an unfinished line kept at the front of the buffer
    bool header = true;
    const auto emit = [&](std::string_view line) {
        if (header) {
            header = false;
        } else if (!line.empty()) {
            onLine(line);
        }
    };

    while (true) {
        if (carried == buffer.size()) buffer.resize(buffer.size() * 2);  // a line longer than the buffer
        const std::size_t got = std::fread(buffer.data() + carried, 1, buffer.size() - carried, file.get());
        if (std::ferror(file.get()) != 0) throw std::runtime_error("read error in " + path.string());

        const char* data = buffer.data();
        const std::size_t available = carried + got;
        std::size_t start = 0;
        while (start < available) {
            const void* newline = std::memchr(data + start, '\n', available - start);
            if (newline == nullptr) break;
            const auto end = static_cast<std::size_t>(static_cast<const char*>(newline) - data);
            emit(std::string_view(data + start, end - start));
            start = end + 1;
        }
        carried = available - start;
        if (carried > 0 && start > 0) std::memmove(buffer.data(), data + start, carried);

        if (got == 0 || std::feof(file.get()) != 0) {
            if (carried > 0) emit(std::string_view(buffer.data(), carried));  // last line without '\n'
            break;
        }
    }
}

// Naive split: a fresh vector of fresh strings for every line.
std::vector<std::string> splitNaive(std::string_view line) {
    std::vector<std::string> fields;
    std::string field;
    bool inQuotes = false;
    for (const char c : line) {
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (c == ',' && !inQuotes) {
            fields.push_back(field);
            field.clear();
        } else if (c != '\r') {
            field += c;
        }
    }
    fields.push_back(field);
    return fields;
}

// ---- row builder ------------------------------------------------------------

// Turns tokenized fields into Measurements. Rows are buffered and handed to
// the store in batches so the store's virtual append runs once per batch.
class RowBuilder {
public:
    static constexpr std::size_t kBatchRows = std::size_t{64} * 1024;

    RowBuilder(MonitorRegistry& registry, Dictionary<CodeId>& qualifiers, Dictionary<CodeId>& methods,
               MeasurementStore& store, LoadReport& report, FileReport& file)
        : registry_(registry), qualifiers_(qualifiers), methods_(methods), store_(store),
          report_(report), file_(file) {
        batch_.reserve(kBatchRows);
    }

    template <typename Numbers>
    void process(const CsvTokenizer::Fields& f, std::size_t count) {
        if (count != kFieldCount) return reject(report_.badFieldCount);

        MonitorKey key;
        std::uint32_t parameter = 0;
        if (!Numbers::integer(f[kState], key.state) || !Numbers::integer(f[kCounty], key.county) ||
            !Numbers::integer(f[kSite], key.site) || !Numbers::integer(f[kParameter], parameter) ||
            !Numbers::integer(f[kPoc], key.poc)) {
            return reject(report_.badNumber);
        }
        if (parameter != parameterCode(file_.pollutant)) return reject(report_.badPollutant);
        key.pollutant = file_.pollutant;

        int year = 0, gmtYear = 0;
        unsigned month = 0, day = 0, hour = 0, gmtMonth = 0, gmtDay = 0, gmtHour = 0;
        if (!Numbers::date(f[kDateLocal], year, month, day) || !Numbers::hour(f[kTimeLocal], hour) ||
            !Numbers::date(f[kDateGmt], gmtYear, gmtMonth, gmtDay) || !Numbers::hour(f[kTimeGmt], gmtHour)) {
            return reject(report_.badDateTime);
        }
        const std::int64_t local = hoursSinceEpoch(year, month, day, hour);
        const std::int64_t gmt = hoursSinceEpoch(gmtYear, gmtMonth, gmtDay, gmtHour);
        if (local < 0 || local > std::numeric_limits<HourIndex>::max()) return reject(report_.badDateTime);

        std::int32_t value = 0;
        if (!Numbers::scaled(f[kSample], valueDecimals(file_.pollutant), value)) return reject(report_.badNumber);
        if (value < std::numeric_limits<ScaledValue>::min() || value > std::numeric_limits<ScaledValue>::max()) {
            return reject(report_.valueOutOfRange);
        }

        const std::int64_t offset = local - gmt;
        if (!monitor_ || key.packed() != currentKey_) {
            if (!switchMonitor<Numbers>(key, f, offset)) return reject(report_.badNumber);
        }
        if (offset != monitor_->utcOffsetHours) ++report_.utcOffsetChanges;
        if (!f[kUncertainty].empty()) ++report_.uncertaintyNonEmpty;

        const auto h = static_cast<HourIndex>(local);
        ++monitor_->rows;
        monitor_->firstHour = std::min(monitor_->firstHour, h);
        monitor_->lastHour = std::max(monitor_->lastHour, h);

        batch_.push_back(Measurement{h, currentId_, static_cast<ScaledValue>(value),
                                     qualifiers_.intern(f[kQualifier]), methods_.intern(f[kMethodCode])});
        ++file_.rows;
        if (batch_.size() == kBatchRows) flush();
    }

    void flush() {
        if (batch_.empty()) return;
        store_.appendBatch(batch_);
        batch_.clear();
    }

private:
    // Called only when the monitor key changes (~1,300 times per file).
    template <typename Numbers>
    bool switchMonitor(const MonitorKey& key, const CsvTokenizer::Fields& f, std::int64_t offset) {
        const auto [id, added] = registry_.findOrAdd(key);
        Monitor& monitor = registry_.at(id);
        if (added) {
            if (!Numbers::real(f[kLatitude], monitor.latitude) || !Numbers::real(f[kLongitude], monitor.longitude) ||
                offset < std::numeric_limits<std::int8_t>::min() || offset > std::numeric_limits<std::int8_t>::max()) {
                return false;  // the entry stays, with default location; the row is rejected
            }
            monitor.utcOffsetHours = static_cast<std::int8_t>(offset);
            monitor.datum = f[kDatum];
            monitor.stateName = f[kStateName];
            monitor.countyName = f[kCountyName];
            monitor.methodType = f[kMethodType];
        }
        monitor_ = &monitor;  // registry only grows here, so the pointer stays valid until the next switch
        currentId_ = id;
        currentKey_ = key.packed();
        return true;
    }

    void reject(std::uint64_t& reason) {
        ++reason;
        ++report_.badRows;
        ++file_.badRows;
    }

    MonitorRegistry& registry_;
    Dictionary<CodeId>& qualifiers_;
    Dictionary<CodeId>& methods_;
    MeasurementStore& store_;
    LoadReport& report_;
    FileReport& file_;

    std::vector<Measurement> batch_;
    Monitor* monitor_ = nullptr;
    MonitorId currentId_ = 0;
    std::uint64_t currentKey_ = 0;
};

}  // namespace

// ---- DataLoader -----------------------------------------------------------------

std::optional<LoaderKind> loaderKindFromName(std::string_view name) {
    if (name == "naive") return LoaderKind::Naive;
    if (name == "getline") return LoaderKind::Getline;
    if (name == "buffered") return LoaderKind::Buffered;
    return std::nullopt;
}

std::string_view loaderKindName(LoaderKind kind) {
    switch (kind) {
        case LoaderKind::Naive: return "naive";
        case LoaderKind::Getline: return "getline";
        case LoaderKind::Buffered: return "buffered";
    }
    return "unknown";
}

DataLoader::DataLoader(MonitorRegistry& registry, Dictionary<CodeId>& qualifiers, Dictionary<CodeId>& methods,
                       MeasurementStore& store)
    : registry_(registry), qualifiers_(qualifiers), methods_(methods), store_(store) {}

std::optional<DataFile> DataLoader::describe(const fs::path& path) {
    // hourly_44201_2024.csv
    const std::string name = path.filename().string();
    if (name.size() != 21 || name.rfind("hourly_", 0) != 0 || name[12] != '_' || name.substr(17) != ".csv") {
        return std::nullopt;
    }
    std::uint32_t code = 0;
    int year = 0;
    if (!parse::unsignedInt(std::string_view(name).substr(7, 5), code) ||
        !parse::unsignedInt(std::string_view(name).substr(13, 4), year)) {
        return std::nullopt;
    }
    const auto pollutant = pollutantFromCode(code);
    if (!pollutant) return std::nullopt;
    return DataFile{path, *pollutant, year, 0};
}

std::vector<DataFile> DataLoader::discover(const fs::path& path, const LoadOptions& options) {
    std::vector<DataFile> files;
    const auto consider = [&](const fs::path& candidate) {
        auto file = describe(candidate);
        if (!file) return;
        if (file->pollutant == Pollutant::Ozone && !options.includeOzone) return;
        if (file->pollutant == Pollutant::NO2 && !options.includeNo2) return;
        if (file->year < options.yearFrom || file->year > options.yearTo) return;
        file->bytes = fs::file_size(candidate);
        files.push_back(*file);
    };

    if (fs::is_regular_file(path)) {
        consider(path);
        if (files.empty() && !describe(path)) {
            throw std::runtime_error(path.string() + " is not named like an EPA hourly file (hourly_<code>_<year>.csv)");
        }
    } else if (fs::is_directory(path)) {
        for (const auto& entry : fs::recursive_directory_iterator(path)) {
            if (entry.is_regular_file()) consider(entry.path());
        }
    } else {
        throw std::runtime_error("data path not found: " + path.string());
    }

    std::sort(files.begin(), files.end(), [](const DataFile& a, const DataFile& b) {
        return std::tie(a.pollutant, a.year, a.path) < std::tie(b.pollutant, b.year, b.path);
    });
    return files;
}

LoadReport DataLoader::load(const fs::path& path, const LoadOptions& options) {
    const std::vector<DataFile> files = discover(path, options);
    if (files.empty()) throw std::runtime_error("no matching EPA hourly CSV files under " + path.string());

    LoadReport report;
    const Stopwatch total;
    if (options.reserve) {
        std::uintmax_t bytes = 0;
        for (const DataFile& file : files) bytes += file.bytes;
        report.reservedRows = static_cast<std::size_t>(bytes / kReserveBytesPerRow) + 1;
        store_.reserve(store_.size() + report.reservedRows);
    }

    for (const DataFile& file : files) {
        FileReport fileReport;
        fileReport.path = file.path;
        fileReport.pollutant = file.pollutant;
        fileReport.year = file.year;
        fileReport.bytes = file.bytes;
        const Stopwatch watch;

        RowBuilder builder(registry_, qualifiers_, methods_, store_, report, fileReport);
        CsvTokenizer::Fields fields;
        store_.beginSegment(file.pollutant);
        switch (options.loader) {
            case LoaderKind::Naive:
                readWithGetline(file.path, [&](std::string_view line) {
                    const std::vector<std::string> parts = splitNaive(line);
                    const std::size_t count = std::min(parts.size(), CsvTokenizer::kMaxFields + 1);
                    for (std::size_t i = 0; i < count && i < CsvTokenizer::kMaxFields; ++i) fields[i] = parts[i];
                    builder.process<NaiveNumbers>(fields, count);
                });
                break;
            case LoaderKind::Getline:
                readWithGetline(file.path, [&](std::string_view line) {
                    builder.process<FastNumbers>(fields, CsvTokenizer::split(line, fields));
                });
                break;
            case LoaderKind::Buffered:
                readBuffered(file.path, options.chunkBytes, [&](std::string_view line) {
                    builder.process<FastNumbers>(fields, CsvTokenizer::split(line, fields));
                });
                break;
        }
        builder.flush();
        store_.endSegment();

        fileReport.seconds = watch.seconds();
        report.bytes += fileReport.bytes;
        report.rows += fileReport.rows;
        report.files.push_back(fileReport);
    }
    report.seconds = total.seconds();
    return report;
}

}  // namespace aq
