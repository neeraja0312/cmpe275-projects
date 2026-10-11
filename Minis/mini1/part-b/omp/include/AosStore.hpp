#pragma once

// Array-of-structs storage: every row is a 12-byte Measurement in one vector,
// in load order. Because the EPA files are grouped by monitor and sorted by
// time, each monitor's rows form a few contiguous, time-sorted blocks (about
// one per year file). Two small indexes are kept while loading:
//   - blocks per monitor  -> Q1 binary-searches inside a monitor's blocks
//   - segments per file   -> Q2/Q3 skip files of the other pollutant or
//                            outside the time range (min/max hour per file)

#include "MeasurementStore.hpp"

#include <cstdint>
#include <vector>

namespace aq {

class AosStore final : public MeasurementStore {
public:
    std::string_view name() const override { return "aos"; }

    void reserve(std::size_t rows) override;
    void beginSegment(Pollutant pollutant) override;
    void appendBatch(std::span<const Measurement> rows) override;
    void endSegment() override;

    std::size_t size() const override { return rows_.size(); }
    std::size_t memoryBytes() const override;
    std::uint64_t orderViolations() const override { return orderViolations_; }

    void rangeByMonitor(MonitorId monitor, HourRange range,
                        std::vector<std::span<const Measurement>>& out) const override;
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

    template <typename OnMatch>
    void scanValues(const ValueQuery& query, OnMatch&& onMatch) const;

    template <GroupBy G>
    void aggregateInto(const AggregateQuery& query, std::vector<Stats>& groups) const;

    std::vector<Measurement> rows_;
    std::vector<std::vector<Block>> blocks_;  // indexed by MonitorId, in time order
    std::vector<Segment> segments_;
    bool segmentOpen_ = false;
    bool startNewBlock_ = true;
    std::uint64_t orderViolations_ = 0;
};

}  // namespace aq
