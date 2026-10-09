#pragma once

// Storage strategy behind the Database facade (private to the library). Two
// implementations share this interface so layouts can be compared on the same
// loader and the same queries:
//
//   AosStore     one array of 12-byte rows (the Phase 1 layout)
//   ColumnStore  one array per field (structure of arrays)
//
// Virtual calls happen once per query or per batch of rows, never per row,
// except in countMatching(), which exists to measure exactly that cost.

#include "aqdata/Database.hpp"
#include "aqdata/Slice.hpp"
#include "aqdata/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace aq {

// A row filter called through a virtual function on every row. Used only to
// measure per-row dynamic dispatch against the templated scans.
class RowPredicate {
public:
    virtual ~RowPredicate() = default;
    virtual bool matches(const Measurement& row) const = 0;
};

// The Q2 condition as a RowPredicate. Defined in its own translation unit so
// the compiler cannot inline it into the store's loop.
class ValueRangePredicate : public RowPredicate {
public:
    explicit ValueRangePredicate(const ValueQuery& query);
    bool matches(const Measurement& row) const override;

private:
    ValueQuery query_;
};

class MeasurementStore {
public:
    virtual ~MeasurementStore() = default;
    virtual std::string_view name() const = 0;

    // Loading. A segment is one input file (one pollutant, one year). Rows of
    // one monitor arrive together and in time order.
    virtual void reserve(std::size_t rows) = 0;
    virtual void beginSegment(Pollutant pollutant) = 0;
    virtual void appendBatch(std::span<const Measurement> rows) = 0;
    virtual void endSegment() = 0;

    virtual std::size_t size() const = 0;
    virtual std::size_t memoryBytes() const = 0;
    virtual std::uint64_t orderViolations() const = 0;

    // Q1: one monitor's readings in a time range, as zero-copy column slices.
    virtual void rangeByMonitor(MonitorId monitor, HourRange range, std::vector<ColumnSlice>& out) const = 0;
    // Q1 as a copy of the rows, using the layout's own fastest path.
    virtual void copyRangeByMonitor(MonitorId monitor, HourRange range, std::vector<Measurement>& out) const = 0;
    // Q1 without the per-monitor index: a full scan (for comparison).
    virtual std::size_t rangeByMonitorScan(MonitorId monitor, HourRange range) const = 0;

    // Q2: readings within a value range.
    virtual std::size_t countByValue(const ValueQuery& query) const = 0;
    virtual void selectByValue(const ValueQuery& query, std::vector<Measurement>& out) const = 0;
    virtual void forEachByValue(const ValueQuery& query, ResultSink& sink) const = 0;
    virtual std::size_t countMatching(Pollutant pollutant, const RowPredicate& predicate) const = 0;

    // Q3: count / min / max / mean, optionally grouped.
    virtual AggregateResult aggregate(const AggregateQuery& query, std::size_t monitorCount) const = 0;
};

// Factory: picks the storage layout, e.g. from a command-line flag.
std::unique_ptr<MeasurementStore> makeStore(Layout layout);

}  // namespace aq
