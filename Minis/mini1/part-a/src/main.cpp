// mini1_a: Phase 1 driver (serial).
//
// Loads EPA AQS hourly data through the AirQualityDB facade, runs one command
// and prints key=value lines, so scripts/run_bench.sh and the report can use
// the output directly. Load time and query time are reported separately.

#include "AirQualityDB.hpp"
#include "Experiments.hpp"
#include "MemoryUsage.hpp"
#include "Parse.hpp"
#include "Time.hpp"
#include "Timer.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace aq;

constexpr std::string_view kUsage = R"(usage: mini1_a <data-path> [options] <command> [args...]

<data-path> is the Dataset directory (searched recursively for
hourly_<code>_<year>.csv) or a single such file.

commands:
  load                                   load only, print load statistics
  monitors [ozone|no2]                   list monitors
  q1 <monitor> <from> <to>               Q1: one monitor's readings in [from, to)
                                         monitor = SS-CCC-SSSS-PPPPP-POC, e.g. 06-037-1103-44201-1
  q2 <ozone|no2> <lo> <hi> [<from> <to>] Q2: readings with lo <= value <= hi (ppm / ppb)
  q3 <ozone|no2> <from> <to> [none|monitor|hour]
                                         Q3: count / min / max / mean
  q4 state <code> [ozone|no2]            Q4: monitors in a state (FIPS code)
  q4 box <latMin> <latMax> <lonMin> <lonMax> [ozone|no2]
  experiment string-rows                 load one CSV with every field as std::string
  experiment float-values                compare exact vs float/double value parsing

times: YYYY-MM-DD or YYYY-MM-DDTHH (local standard time); ranges are [from, to)

options:
  --loader naive|getline|buffered   reader version (default buffered)
  --no-reserve                      do not pre-size storage
  --chunk-mb N                      buffered reader chunk size (default 8)
  --pollutant ozone|no2|both        files to load (default both)
  --years A or A-B                  years to load (default all)
  --mode M                          q1: copy|view|scan   q2: count|copy|callback|virtual
  --repeat N                        run the query N times in this process (default 1)
  --print N                         print up to N result rows / groups (default 10)
)";

struct Cli {
    std::string dataPath;
    std::string command;
    std::vector<std::string> args;
    LoadOptions load;
    std::string mode;
    int repeat = 1;
    std::size_t print = 10;
};

[[noreturn]] void fail(const std::string& message) { throw std::invalid_argument(message); }

template <typename T>
T number(std::string_view text, const char* what) {
    T value{};
    if (!parse::unsignedInt(text, value)) fail(std::string("invalid ") + what + ": " + std::string(text));
    return value;
}

double realNumber(std::string_view text, const char* what) {
    double value = 0;
    if (!parse::real(text, value)) fail(std::string("invalid ") + what + ": " + std::string(text));
    return value;
}

Cli parseCli(int argc, char** argv) {
    Cli cli;
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const auto value = [&]() -> std::string_view {
            if (i + 1 >= argc) fail(std::string(arg) + " needs a value");
            return argv[++i];
        };
        if (arg == "--help" || arg == "-h") {
            std::cout << kUsage;
            std::exit(0);
        } else if (arg == "--loader") {
            const auto kind = loaderKindFromName(value());
            if (!kind) fail("unknown loader (naive|getline|buffered)");
            cli.load.loader = *kind;
        } else if (arg == "--no-reserve") {
            cli.load.reserve = false;
        } else if (arg == "--chunk-mb") {
            cli.load.chunkBytes = number<std::size_t>(value(), "--chunk-mb") * 1024 * 1024;
        } else if (arg == "--pollutant") {
            const std::string_view p = value();
            cli.load.includeOzone = p == "ozone" || p == "both";
            cli.load.includeNo2 = p == "no2" || p == "both";
            if (!cli.load.includeOzone && !cli.load.includeNo2) fail("--pollutant must be ozone, no2 or both");
        } else if (arg == "--years") {
            const std::string_view years = value();
            const std::size_t dash = years.find('-');
            cli.load.yearFrom = number<int>(years.substr(0, dash), "--years");
            cli.load.yearTo = dash == std::string_view::npos ? cli.load.yearFrom
                                                             : number<int>(years.substr(dash + 1), "--years");
        } else if (arg == "--mode") {
            cli.mode = value();
        } else if (arg == "--repeat") {
            cli.repeat = std::max(1, number<int>(value(), "--repeat"));
        } else if (arg == "--print") {
            cli.print = number<std::size_t>(value(), "--print");
        } else if (arg.size() > 2 && arg.substr(0, 2) == "--") {
            fail("unknown option " + std::string(arg));
        } else {
            positional.emplace_back(arg);
        }
    }
    if (positional.size() < 2) fail("need <data-path> and <command> (see --help)");
    cli.dataPath = positional[0];
    cli.command = positional[1];
    cli.args.assign(positional.begin() + 2, positional.end());
    return cli;
}

Pollutant pollutantArg(std::string_view text) {
    const auto p = pollutantFromName(text);
    if (!p) fail("unknown pollutant " + std::string(text) + " (ozone|no2)");
    return *p;
}

HourIndex hourArg(std::string_view text) {
    const auto h = parseHourArg(text);
    if (!h) fail("invalid time " + std::string(text) + " (YYYY-MM-DD or YYYY-MM-DDTHH)");
    return *h;
}

ScaledValue valueArg(Pollutant pollutant, std::string_view text) {
    std::int32_t scaled = 0;
    if (!parse::scaled(text, valueDecimals(pollutant), scaled) || scaled < std::numeric_limits<ScaledValue>::min() ||
        scaled > std::numeric_limits<ScaledValue>::max()) {
        fail("invalid value " + std::string(text));
    }
    return static_cast<ScaledValue>(scaled);
}

std::string valueText(Pollutant pollutant, std::int64_t scaled) {
    return formatScaled(static_cast<std::int32_t>(scaled), valueDecimals(pollutant));
}

std::string meanText(Pollutant pollutant, const Stats& stats) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(valueDecimals(pollutant) + 2)
        << stats.mean() / valueScale(pollutant);
    return out.str();
}

// Runs the query `repeat` times and prints min / mean wall time.
template <typename Query>
auto timed(const Cli& cli, Query&& query) {
    std::vector<double> seconds;
    seconds.reserve(static_cast<std::size_t>(cli.repeat));
    auto result = query();
    for (int i = 0; i < cli.repeat; ++i) {
        const Stopwatch watch;
        result = query();
        seconds.push_back(watch.seconds());
    }
    const double total = std::accumulate(seconds.begin(), seconds.end(), 0.0);
    std::cout << "repeat=" << cli.repeat << '\n'
              << "query_s_min=" << *std::min_element(seconds.begin(), seconds.end()) << '\n'
              << "query_s_mean=" << total / static_cast<double>(seconds.size()) << '\n';
    return result;
}

void printRow(const AirQualityDB& db, const Measurement& row) {
    const Monitor& m = db.monitor(row.monitor);
    std::cout << "row monitor=" << toString(m.key) << " hour=" << formatHour(row.hour)
              << " value=" << valueText(m.key.pollutant, row.value) << ' ' << unitName(m.key.pollutant)
              << " qualifier=" << db.qualifierName(row.qualifier) << " method=" << db.methodName(row.method)
              << '\n';
}

void printMonitor(const AirQualityDB& db, MonitorId id) {
    const Monitor& m = db.monitor(id);
    std::cout << "monitor " << toString(m.key) << " lat=" << m.latitude << " lon=" << m.longitude
              << " state=\"" << m.stateName << "\" county=\"" << m.countyName << "\" rows=" << m.rows
              << " first=" << formatHour(m.firstHour) << " last=" << formatHour(m.lastHour)
              << " utc_offset=" << int{m.utcOffsetHours} << '\n';
}

void printLoad(const AirQualityDB& db, const LoadReport& report, const LoadOptions& options) {
    for (const FileReport& file : report.files) {
        std::cout << "file=" << file.path.filename().string() << " rows=" << file.rows << " bad_rows=" << file.badRows
                  << " bytes=" << file.bytes << " seconds=" << file.seconds << '\n';
    }
    const double mb = static_cast<double>(report.bytes) / (1024.0 * 1024.0);
    const std::size_t rss = peakRssBytes();
    const auto perRow = [&](double bytes) { return report.rows == 0 ? 0.0 : bytes / static_cast<double>(report.rows); };
    std::cout << "store=" << db.storeName() << '\n'
              << "loader=" << loaderKindName(options.loader) << '\n'
              << "reserve=" << (options.reserve ? "on" : "off") << '\n'
              << "files=" << report.files.size() << '\n'
              << "rows=" << report.rows << '\n'
              << "rows_ozone=" << db.rowCount(Pollutant::Ozone) << '\n'
              << "rows_no2=" << db.rowCount(Pollutant::NO2) << '\n'
              << "bad_rows=" << report.badRows << '\n'
              << "bad_field_count=" << report.badFieldCount << '\n'
              << "bad_number=" << report.badNumber << '\n'
              << "bad_pollutant=" << report.badPollutant << '\n'
              << "bad_datetime=" << report.badDateTime << '\n'
              << "value_out_of_range=" << report.valueOutOfRange << '\n'
              << "bytes=" << report.bytes << '\n'
              << "load_s=" << report.seconds << '\n'
              << "mb_per_s=" << (report.seconds > 0 ? mb / report.seconds : 0.0) << '\n'
              << "rows_per_s=" << (report.seconds > 0 ? static_cast<double>(report.rows) / report.seconds : 0.0) << '\n'
              << "monitors=" << db.monitorCount() << '\n'
              << "qualifier_codes=" << db.qualifierCount() - 1 << '\n'
              << "method_codes=" << db.methodCount() - 1 << '\n'
              << "store_bytes=" << db.storeBytes() << '\n'
              << "store_bytes_per_row=" << perRow(static_cast<double>(db.storeBytes())) << '\n'
              << "peak_rss_bytes=" << rss << '\n'
              << "rss_bytes_per_row=" << perRow(static_cast<double>(rss)) << '\n'
              << "peak_footprint_bytes=" << peakFootprintBytes() << '\n'
              << "footprint_bytes_per_row=" << perRow(static_cast<double>(peakFootprintBytes())) << '\n'
              << "uncertainty_nonempty=" << report.uncertaintyNonEmpty << '\n'
              << "utc_offset_changes=" << report.utcOffsetChanges << '\n'
              << "order_violations=" << db.orderViolations() << '\n';
}

// ---- commands -----------------------------------------------------------------

void runMonitors(const AirQualityDB& db, const Cli& cli) {
    const std::optional<Pollutant> pollutant =
        cli.args.empty() ? std::nullopt : std::optional<Pollutant>(pollutantArg(cli.args[0]));
    std::size_t shown = 0;
    std::size_t matched = 0;
    for (std::size_t i = 0; i < db.monitorCount(); ++i) {
        const auto id = static_cast<MonitorId>(i);
        if (pollutant && db.monitor(id).key.pollutant != *pollutant) continue;
        ++matched;
        if (shown++ < cli.print) printMonitor(db, id);
    }
    std::cout << "monitors_listed=" << matched << '\n';
}

void runQ1(const AirQualityDB& db, const Cli& cli) {
    if (cli.args.size() != 3) fail("q1 needs <monitor> <from> <to>");
    const auto key = parseMonitorKey(cli.args[0]);
    if (!key) fail("invalid monitor " + cli.args[0] + " (SS-CCC-SSSS-PPPPP-POC)");
    const auto id = db.findMonitor(*key);
    if (!id) fail("monitor " + cli.args[0] + " not found in the loaded data");
    const HourRange range{hourArg(cli.args[1]), hourArg(cli.args[2])};
    const std::string mode = cli.mode.empty() ? "copy" : cli.mode;

    std::cout << "query=q1\nmode=" << mode << "\nmonitor=" << cli.args[0] << '\n';
    std::size_t results = 0;
    std::int64_t checksum = 0;
    std::vector<Measurement> sample;
    if (mode == "copy") {
        const auto rows = timed(cli, [&] { return db.rangeByMonitor(*id, range); });
        results = rows.size();
        for (const auto& r : rows) checksum += r.value;
        sample.assign(rows.begin(), rows.begin() + static_cast<std::ptrdiff_t>(std::min(cli.print, rows.size())));
    } else if (mode == "view") {
        const auto views = timed(cli, [&] { return db.rangeByMonitorView(*id, range); });
        for (const auto& view : views) {
            results += view.size();
            for (const auto& r : view) {
                checksum += r.value;
                if (sample.size() < cli.print) sample.push_back(r);
            }
        }
        std::cout << "views=" << views.size() << '\n';
    } else if (mode == "scan") {
        results = timed(cli, [&] { return db.rangeByMonitorScan(*id, range); });
    } else {
        fail("q1 --mode must be copy, view or scan");
    }
    std::cout << "results=" << results << '\n';
    if (mode != "scan") std::cout << "checksum=" << checksum << '\n';
    for (const auto& row : sample) printRow(db, row);
}

class ChecksumSink final : public ResultSink {
public:
    explicit ChecksumSink(std::size_t keep) : keep_(keep) {}
    void onBatch(std::span<const Measurement> rows) override {
        ++batches;
        for (const Measurement& row : rows) {
            ++count;
            checksum += row.value;
            if (sample.size() < keep_) sample.push_back(row);
        }
    }
    std::size_t batches = 0;
    std::size_t count = 0;
    std::int64_t checksum = 0;
    std::vector<Measurement> sample;

private:
    std::size_t keep_;
};

void runQ2(const AirQualityDB& db, const Cli& cli) {
    if (cli.args.size() != 3 && cli.args.size() != 5) fail("q2 needs <ozone|no2> <lo> <hi> [<from> <to>]");
    ValueQuery query;
    query.pollutant = pollutantArg(cli.args[0]);
    query.lo = valueArg(query.pollutant, cli.args[1]);
    query.hi = valueArg(query.pollutant, cli.args[2]);
    if (cli.args.size() == 5) query.range = {hourArg(cli.args[3]), hourArg(cli.args[4])};
    const std::string mode = cli.mode.empty() ? "count" : cli.mode;

    std::cout << "query=q2\nmode=" << mode << "\npollutant=" << pollutantName(query.pollutant) << '\n';
    std::size_t results = 0;
    std::optional<std::int64_t> checksum;
    std::vector<Measurement> sample;
    if (mode == "count") {
        results = timed(cli, [&] { return db.countByValue(query); });
    } else if (mode == "virtual") {
        results = timed(cli, [&] { return db.countByValueVirtual(query); });
    } else if (mode == "copy") {
        const auto rows = timed(cli, [&] { return db.selectByValue(query); });
        results = rows.size();
        checksum = 0;
        for (const auto& r : rows) *checksum += r.value;
        sample.assign(rows.begin(), rows.begin() + static_cast<std::ptrdiff_t>(std::min(cli.print, rows.size())));
    } else if (mode == "callback") {
        const auto sink = timed(cli, [&] {
            ChecksumSink s(cli.print);
            db.forEachByValue(query, s);
            return s;
        });
        results = sink.count;
        checksum = sink.checksum;
        sample = sink.sample;
        std::cout << "batches=" << sink.batches << '\n';
    } else {
        fail("q2 --mode must be count, copy, callback or virtual");
    }

    std::cout << "results=" << results << '\n';
    if (checksum) std::cout << "checksum=" << *checksum << '\n';
    if (cli.args.size() == 3) {
        const std::uint64_t total = db.rowCount(query.pollutant);
        std::cout << "pollutant_rows=" << total << '\n'
                  << "selectivity_pct="
                  << (total == 0 ? 0.0 : 100.0 * static_cast<double>(results) / static_cast<double>(total)) << '\n';
    }
    for (const auto& row : sample) printRow(db, row);
}

void runQ3(const AirQualityDB& db, const Cli& cli) {
    if (cli.args.size() != 3 && cli.args.size() != 4) fail("q3 needs <ozone|no2> <from> <to> [none|monitor|hour]");
    AggregateQuery query;
    query.pollutant = pollutantArg(cli.args[0]);
    query.range = {hourArg(cli.args[1]), hourArg(cli.args[2])};
    const std::string group = cli.args.size() == 4 ? cli.args[3] : "none";
    if (group == "none") {
        query.groupBy = GroupBy::None;
    } else if (group == "monitor") {
        query.groupBy = GroupBy::Monitor;
    } else if (group == "hour") {
        query.groupBy = GroupBy::HourOfDay;
    } else {
        fail("q3 group must be none, monitor or hour");
    }

    std::cout << "query=q3\ngroup=" << group << "\npollutant=" << pollutantName(query.pollutant) << '\n';
    const AggregateResult result = timed(cli, [&] { return db.aggregate(query); });

    Stats total;
    std::size_t nonEmpty = 0;
    for (const Stats& s : result.groups) {
        total.merge(s);
        if (s.count > 0) ++nonEmpty;
    }
    const auto statsText = [&](const Stats& s) {
        std::ostringstream out;
        out << "count=" << s.count;
        if (s.count > 0) {
            out << " min=" << valueText(query.pollutant, s.min) << " max=" << valueText(query.pollutant, s.max)
                << " mean=" << meanText(query.pollutant, s);
        }
        return out.str();
    };
    std::cout << "groups=" << result.groups.size() << "\nnonempty_groups=" << nonEmpty << '\n'
              << "results=" << total.count << "\nchecksum=" << total.sum << '\n'
              << "total " << statsText(total) << " unit=" << unitName(query.pollutant) << '\n';

    std::size_t shown = 0;
    for (std::size_t i = 0; i < result.groups.size() && shown < cli.print; ++i) {
        const Stats& s = result.groups[i];
        if (s.count == 0) continue;
        ++shown;
        if (query.groupBy == GroupBy::HourOfDay) {
            std::cout << "group hour=" << std::setw(2) << std::setfill('0') << i << std::setfill(' ') << ' '
                      << statsText(s) << '\n';
        } else if (query.groupBy == GroupBy::Monitor) {
            std::cout << "group monitor=" << toString(db.monitor(static_cast<MonitorId>(i)).key) << ' '
                      << statsText(s) << '\n';
        }
    }
}

void runQ4(const AirQualityDB& db, const Cli& cli) {
    if (cli.args.empty()) fail("q4 needs 'state <code>' or 'box <latMin> <latMax> <lonMin> <lonMax>'");
    std::vector<MonitorId> ids;
    std::optional<Pollutant> pollutant;
    if (cli.args[0] == "state") {
        if (cli.args.size() != 2 && cli.args.size() != 3) fail("q4 state <code> [ozone|no2]");
        if (cli.args.size() == 3) pollutant = pollutantArg(cli.args[2]);
        const unsigned state = number<unsigned>(cli.args[1], "state code");
        ids = timed(cli, [&] { return db.monitorsInState(state, pollutant); });
    } else if (cli.args[0] == "box") {
        if (cli.args.size() != 5 && cli.args.size() != 6) fail("q4 box <latMin> <latMax> <lonMin> <lonMax> [ozone|no2]");
        if (cli.args.size() == 6) pollutant = pollutantArg(cli.args[5]);
        const double latMin = realNumber(cli.args[1], "latMin");
        const double latMax = realNumber(cli.args[2], "latMax");
        const double lonMin = realNumber(cli.args[3], "lonMin");
        const double lonMax = realNumber(cli.args[4], "lonMax");
        ids = timed(cli, [&] { return db.monitorsInBox(latMin, latMax, lonMin, lonMax, pollutant); });
    } else {
        fail("q4 needs 'state' or 'box'");
    }
    std::cout << "query=q4\nresults=" << ids.size() << '\n';
    for (std::size_t i = 0; i < ids.size() && i < cli.print; ++i) printMonitor(db, ids[i]);
}

void runExperiment(const Cli& cli) {
    if (cli.args.size() != 1) fail("experiment needs string-rows or float-values");
    constexpr double kDatasetRows = 65'742'181.0;  // all 12 files
    if (cli.args[0] == "string-rows") {
        const StringRowsResult r = loadAsStringRows(cli.dataPath);
        std::cout << "experiment=string-rows\nrows=" << r.rows << "\nseconds=" << r.seconds
                  << "\nbytes_per_row=" << r.bytesPerRow << "\npeak_rss_bytes=" << r.peakRssBytes
                  << "\nextrapolated_dataset_bytes=" << static_cast<double>(r.bytesPerRow) * kDatasetRows
                  << "\ncompact_dataset_bytes=" << sizeof(Measurement) * kDatasetRows << '\n';
    } else if (cli.args[0] == "float-values") {
        const FloatValuesResult r = compareValueParsing(cli.dataPath);
        std::cout << "experiment=float-values\nrows=" << r.rows << "\nfloat_truncated_wrong=" << r.floatTruncated
                  << "\nfloat_rounded_wrong=" << r.floatRounded << "\ndouble_truncated_wrong=" << r.doubleTruncated
                  << "\ndouble_rounded_wrong=" << r.doubleRounded << "\nrows_at_threshold=" << r.atThreshold
                  << "\nfloat_threshold_wrong=" << r.floatThreshold
                  << "\ndouble_threshold_wrong=" << r.doubleThreshold << '\n';
    } else {
        fail("unknown experiment " + cli.args[0]);
    }
}

int run(int argc, char** argv) {
    const Cli cli = parseCli(argc, argv);
    std::cout << std::setprecision(6);

    if (cli.command == "experiment") {
        runExperiment(cli);
        return 0;
    }

    AirQualityDB db;
    const LoadReport report = db.load(cli.dataPath, cli.load);
    printLoad(db, report, cli.load);

    if (cli.command == "load") return 0;
    if (cli.command == "monitors") {
        runMonitors(db, cli);
    } else if (cli.command == "q1") {
        runQ1(db, cli);
    } else if (cli.command == "q2") {
        runQ2(db, cli);
    } else if (cli.command == "q3") {
        runQ3(db, cli);
    } else if (cli.command == "q4") {
        runQ4(db, cli);
    } else {
        fail("unknown command " + cli.command + " (see --help)");
    }
    std::cout << "peak_rss_bytes_end=" << peakRssBytes() << '\n'
              << "peak_footprint_bytes_end=" << peakFootprintBytes() << '\n';
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::invalid_argument& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
