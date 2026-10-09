#pragma once

// Structure-of-arrays storage: one contiguous array per field (hour, monitor,
// value, qualifier, method) instead of one array of 12-byte rows. The queries
// decide the layout:
//
//   Q2 count / Q3 none   read only the value column (2 bytes per row, not 12)
//   Q3 by hour           value + hour columns (6 bytes per row)
//   Q3 by monitor        value + monitor columns (4 bytes per row)
//   Q1                   binary search on the hour column only
//
// The same two small indexes as the row layout are kept while loading (blocks
// per monitor for Q1, segments per file for Q2/Q3), so the comparison between
// layouts isolates the memory layout and nothing else. Rows are rebuilt as
// Measurement values only when a result is returned by copy.

#include "MeasurementStore.hpp"

#include <cstdint>
#include <vector>

namespace aq {

class ColumnStore final : public MeasurementStore {
public:
    std::string_view name() const override { return "columns"; }

    void reserve(std::size_t rows) override;
    void beginSegment(Pollutant pollutant) override;
    void appendBatch(std::span<const Measurement> rows) override;
    void endSegment() override;

    std::size_t size() const override { return values_.size(); }
    std::size_t memoryBytes() const override;
    std::uint64_t orderViolations() const override { return orderViolations_; }

    void rangeByMonitor(MonitorId monitor, HourRange range, std::vector<ColumnSlice>& out) const override;
    void copyRangeByMonitor(MonitorId monitor, HourRange range, std::vector<Measurement>& out) const override;
    std::size_t rangeByMonitorScan(MonitorId monitor, HourRange range) const override;

    std::size_t countByValue(const ValueQuery& query) const override;
    void selectByValue(const ValueQuery& query, std::vector<Measurement>& out) const override;
    void forEachByValue(const ValueQuery& query, ResultSink& sink) const override;
    std::size_t countMatching(Pollutant pollutant, const RowPredicate& predicate) const override;

    AggregateResult aggregate(const AggregateQuery& query, std::size_t monitorCount) const override;

private:
    struct Block {
        std::uint32_t begin;
        std::uint32_t count;
        HourIndex first;
        HourIndex last;
    };
    struct Segment {
        Pollutant pollutant;
        std::uint32_t begin;
        std::uint32_t end;
        HourIndex minHour;
        HourIndex maxHour;
    };

    Measurement rowAt(std::size_t i) const {
        return Measurement{hours_[i], monitors_[i], values_[i], qualifiers_[i], methods_[i]};
    }

    // Calls onMatch(index) for every row whose value (and time) match.
    template <typename OnMatch>
    void scanValues(const ValueQuery& query, OnMatch&& onMatch) const;

    template <GroupBy G>
    void aggregateInto(const AggregateQuery& query, std::vector<Stats>& groups) const;

    std::vector<HourIndex> hours_;
    std::vector<MonitorId> monitors_;
    std::vector<ScaledValue> values_;
    std::vector<CodeId> qualifiers_;
    std::vector<CodeId> methods_;
    std::vector<std::vector<Block>> blocks_;  // indexed by MonitorId, in time order
    std::vector<Segment> segments_;
    bool segmentOpen_ = false;
    bool startNewBlock_ = true;
    std::uint64_t orderViolations_ = 0;
};

}  // namespace aq
