#include "DataLoader.hpp"

#include "CsvTokenizer.hpp"
#include "Parse.hpp"
#include "Time.hpp"
#include "Timer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <memory>
#include <omp.h>
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
               std::vector<Measurement>& rows, LoadReport& report, FileReport& file)
        : registry_(registry), qualifiers_(qualifiers), methods_(methods), rows_(rows),
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
        rows_.insert(rows_.end(), batch_.begin(), batch_.end());
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
    std::vector<Measurement>& rows_;
    LoadReport& report_;
    FileReport& file_;

    std::vector<Measurement> batch_;
    Monitor* monitor_ = nullptr;
    MonitorId currentId_ = 0;
    std::uint64_t currentKey_ = 0;
};

struct FileLoadResult {
    MonitorRegistry registry;
    Dictionary<CodeId> qualifiers;
    Dictionary<CodeId> methods;
    std::vector<Measurement> rows;
    LoadReport report;
    FileReport file;
    std::exception_ptr error;
};

void loadFile(const DataFile& source, const LoadOptions& options, FileLoadResult& result) {
    result.file.path = source.path;
    result.file.pollutant = source.pollutant;
    result.file.year = source.year;
    result.file.bytes = source.bytes;
    if (options.reserve) result.rows.reserve(static_cast<std::size_t>(source.bytes / kReserveBytesPerRow) + 1);

    const Stopwatch watch;
    RowBuilder builder(result.registry, result.qualifiers, result.methods, result.rows, result.report, result.file);
    CsvTokenizer::Fields fields;
    switch (options.loader) {
        case LoaderKind::Naive:
            readWithGetline(source.path, [&](std::string_view line) {
                const std::vector<std::string> parts = splitNaive(line);
                const std::size_t count = std::min(parts.size(), CsvTokenizer::kMaxFields + 1);
                for (std::size_t i = 0; i < count && i < CsvTokenizer::kMaxFields; ++i) fields[i] = parts[i];
                builder.process<NaiveNumbers>(fields, count);
            });
            break;
        case LoaderKind::Getline:
            readWithGetline(source.path, [&](std::string_view line) {
                builder.process<FastNumbers>(fields, CsvTokenizer::split(line, fields));
            });
            break;
        case LoaderKind::Buffered:
            readBuffered(source.path, options.chunkBytes, [&](std::string_view line) {
                builder.process<FastNumbers>(fields, CsvTokenizer::split(line, fields));
            });
            break;
    }
    builder.flush();
    result.file.seconds = watch.seconds();
}

void mergeCounters(LoadReport& destination, const LoadReport& source) {
    destination.badRows += source.badRows;
    destination.badFieldCount += source.badFieldCount;
    destination.badNumber += source.badNumber;
    destination.badPollutant += source.badPollutant;
    destination.badDateTime += source.badDateTime;
    destination.valueOutOfRange += source.valueOutOfRange;
    destination.uncertaintyNonEmpty += source.uncertaintyNonEmpty;
    destination.utcOffsetChanges += source.utcOffsetChanges;
}

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

    std::vector<FileLoadResult> loaded(files.size());
#pragma omp parallel for schedule(dynamic)
    for (std::ptrdiff_t i = 0; i < static_cast<std::ptrdiff_t>(files.size()); ++i) {
        try {
            loadFile(files[static_cast<std::size_t>(i)], options, loaded[static_cast<std::size_t>(i)]);
        } catch (...) {
            loaded[static_cast<std::size_t>(i)].error = std::current_exception();
        }
    }
    for (const FileLoadResult& result : loaded) {
        if (result.error) std::rethrow_exception(result.error);
    }

    // Merge in discovery order so monitor/dictionary ids and row order are
    // identical at every thread count.
    for (FileLoadResult& result : loaded) {
        mergeCounters(report, result.report);

        std::vector<MonitorId> monitorIds(result.registry.size());
        for (std::size_t i = 0; i < result.registry.size(); ++i) {
            const Monitor& source = result.registry.at(static_cast<MonitorId>(i));
            const auto [id, added] = registry_.findOrAdd(source.key);
            Monitor& destination = registry_.at(id);
            if (added) {
                destination = source;
            } else {
                if (destination.utcOffsetHours != source.utcOffsetHours) {
                    report.utcOffsetChanges += source.rows;
                }
                destination.rows += source.rows;
                destination.firstHour = std::min(destination.firstHour, source.firstHour);
                destination.lastHour = std::max(destination.lastHour, source.lastHour);
            }
            monitorIds[i] = id;
        }

        std::vector<CodeId> qualifierIds(result.qualifiers.size());
        for (std::size_t i = 0; i < result.qualifiers.size(); ++i) {
            qualifierIds[i] = qualifiers_.intern(result.qualifiers.name(static_cast<CodeId>(i)));
        }
        std::vector<CodeId> methodIds(result.methods.size());
        for (std::size_t i = 0; i < result.methods.size(); ++i) {
            methodIds[i] = methods_.intern(result.methods.name(static_cast<CodeId>(i)));
        }

        for (Measurement& row : result.rows) {
            row.monitor = monitorIds[row.monitor];
            row.qualifier = qualifierIds[row.qualifier];
            row.method = methodIds[row.method];
        }
        store_.beginSegment(result.file.pollutant);
        store_.appendBatch(result.rows);
        store_.endSegment();

        report.bytes += result.file.bytes;
        report.rows += result.file.rows;
        report.files.push_back(result.file);
        std::vector<Measurement>().swap(result.rows);
    }
    report.seconds = total.seconds();
    return report;
}

}  // namespace aq
