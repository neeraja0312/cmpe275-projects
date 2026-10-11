#pragma once

// The library's public face (facade). An application includes only the headers
// in aqdata/ and links libaqdata; the storage layouts, the CSV loader, the
// tokenizer and the registries are private to the library (lib/src), reached
// only through the hidden implementation behind this class.
//
//   Q1  rangeByMonitor*  one monitor's readings in a time range
//   Q2  ...ByValue       readings of one pollutant within a value range
//   Q3  aggregate        count / min / max / mean, optionally grouped
//   Q4  monitorsIn...    monitors in a state or a lat/lon box
//
// Several Q1/Q2 variants return the same answer in different ways (copy,
// view, callback, count) so the cost of moving results across this boundary
// can be measured.

#include "aqdata/Export.hpp"
#include "aqdata/Load.hpp"
#include "aqdata/Slice.hpp"
#include "aqdata/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace aq {

// How the library keeps the rows in memory.
//   Aos     - one array of 12-byte rows (the Phase 1 layout)
//   Columns - one array per field; a value-range search reads 2 bytes per row
enum class Layout : std::uint8_t { Aos, Columns };

AQ_API std::optional<Layout> layoutFromName(std::string_view name);
AQ_API std::string_view layoutName(Layout layout);

// Receives query results in batches, so large results can cross the library
// edge without one big copy.
class ResultSink {
public:
    virtual ~ResultSink() = default;
    virtual void onBatch(std::span<const Measurement> rows) = 0;
};

class AQ_API Database {
public:
    explicit Database(Layout layout = Layout::Aos);
    ~Database();
    Database(Database&&) noexcept;
    Database& operator=(Database&&) noexcept;
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    LoadReport load(const std::filesystem::path& path, const LoadOptions& options = {});

    // ---- Q1: monitor + time range
    std::optional<MonitorId> findMonitor(const MonitorKey& key) const;
    std::vector<Measurement> rangeByMonitor(MonitorId monitor, HourRange range) const;           // copy
    std::vector<ColumnSlice> rangeByMonitorView(MonitorId monitor, HourRange range) const;       // zero-copy
    std::size_t rangeByMonitorScan(MonitorId monitor, HourRange range) const;                    // no index

    // ---- Q2: value range
    std::size_t countByValue(const ValueQuery& query) const;
    std::vector<Measurement> selectByValue(const ValueQuery& query) const;                       // copy
    void forEachByValue(const ValueQuery& query, ResultSink& sink) const;                        // batched callback
    std::size_t countByValueVirtual(const ValueQuery& query) const;                              // virtual call per row

    // ---- Q3: aggregates
    AggregateResult aggregate(const AggregateQuery& query) const;

    // ---- Q4: monitors by place (any pollutant when none is given)
    std::vector<MonitorId> monitorsInState(unsigned stateCode, std::optional<Pollutant> pollutant = {}) const;
    std::vector<MonitorId> monitorsInBox(double latMin, double latMax, double lonMin, double lonMax,
                                         std::optional<Pollutant> pollutant = {}) const;

    // ---- metadata
    const Monitor& monitor(MonitorId id) const;
    std::size_t monitorCount() const;
    std::size_t rowCount() const;
    std::uint64_t rowCount(Pollutant pollutant) const;
    std::size_t storeBytes() const;
    std::uint64_t orderViolations() const;
    Layout layout() const;
    std::string_view storeName() const;
    const std::string& qualifierName(CodeId id) const;
    const std::string& methodName(CodeId id) const;
    std::size_t qualifierCount() const;
    std::size_t methodCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace aq
