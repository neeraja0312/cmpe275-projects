#pragma once

// Facade: the only class an application needs. It owns the monitor registry,
// the code dictionaries and the storage strategy, and exposes loading plus the
// four searches:
//
//   Q1  rangeByMonitor  one monitor's readings in a time range
//   Q2  ...ByValue      readings of one pollutant within a value range
//   Q3  aggregate       count / min / max / mean, optionally grouped
//   Q4  monitorsIn...   monitors in a state or a lat/lon box
//
// Several Q1/Q2 variants return the same answer in different ways (copy,
// view, callback, count) so the cost of moving results across this boundary
// can be measured.

#include "DataLoader.hpp"
#include "Dictionary.hpp"
#include "MeasurementStore.hpp"
#include "MonitorRegistry.hpp"
#include "Types.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace aq {

class AirQualityDB {
public:
    explicit AirQualityDB(StoreKind kind = StoreKind::Aos);

    LoadReport load(const std::filesystem::path& path, const LoadOptions& options = {});

    // ---- Q1: monitor + time range
    std::optional<MonitorId> findMonitor(const MonitorKey& key) const;
    std::vector<Measurement> rangeByMonitor(MonitorId monitor, HourRange range) const;               // copy
    std::vector<std::span<const Measurement>> rangeByMonitorView(MonitorId monitor, HourRange range) const;  // zero-copy
    std::size_t rangeByMonitorScan(MonitorId monitor, HourRange range) const;                         // no index

    // ---- Q2: value range
    std::size_t countByValue(const ValueQuery& query) const;
    std::vector<Measurement> selectByValue(const ValueQuery& query) const;                             // copy
    void forEachByValue(const ValueQuery& query, ResultSink& sink) const;                              // batched callback
    std::size_t countByValueVirtual(const ValueQuery& query) const;                                    // virtual call per row

    // ---- Q3: aggregates
    AggregateResult aggregate(const AggregateQuery& query) const;

    // ---- Q4: monitors by place (any pollutant when none is given)
    std::vector<MonitorId> monitorsInState(unsigned stateCode, std::optional<Pollutant> pollutant = {}) const;
    std::vector<MonitorId> monitorsInBox(double latMin, double latMax, double lonMin, double lonMax,
                                         std::optional<Pollutant> pollutant = {}) const;

    // ---- metadata
    const Monitor& monitor(MonitorId id) const { return registry_.at(id); }
    std::size_t monitorCount() const { return registry_.size(); }
    std::size_t rowCount() const { return store_->size(); }
    std::uint64_t rowCount(Pollutant pollutant) const;
    std::size_t storeBytes() const { return store_->memoryBytes(); }
    std::uint64_t orderViolations() const { return store_->orderViolations(); }
    std::string_view storeName() const { return store_->name(); }
    const std::string& qualifierName(CodeId id) const { return qualifiers_.name(id); }
    const std::string& methodName(CodeId id) const { return methods_.name(id); }
    std::size_t qualifierCount() const { return qualifiers_.size(); }
    std::size_t methodCount() const { return methods_.size(); }

private:
    MonitorRegistry registry_;
    Dictionary<CodeId> qualifiers_;
    Dictionary<CodeId> methods_;
    std::unique_ptr<MeasurementStore> store_;
};

}  // namespace aq
