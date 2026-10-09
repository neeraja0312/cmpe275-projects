#include "ColumnStore.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace aq {

void ColumnStore::reserve(std::size_t rows) {
    hours_.reserve(rows);
    monitors_.reserve(rows);
    values_.reserve(rows);
    qualifiers_.reserve(rows);
    methods_.reserve(rows);
}

void ColumnStore::beginSegment(Pollutant pollutant) {
    if (segmentOpen_) endSegment();
    const auto start = static_cast<std::uint32_t>(values_.size());
    segments_.push_back({pollutant, start, start, std::numeric_limits<HourIndex>::max(), 0});
    segmentOpen_ = true;
    startNewBlock_ = true;
}

void ColumnStore::appendBatch(std::span<const Measurement> batch) {
    if (!segmentOpen_) throw std::logic_error("ColumnStore::appendBatch called outside a segment");
    if (values_.size() + batch.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::overflow_error("ColumnStore: more rows than a 32-bit row index can address");
    }

    Segment& segment = segments_.back();
    for (const Measurement& row : batch) {
        const auto index = static_cast<std::uint32_t>(values_.size());
        if (startNewBlock_ || row.monitor != monitors_.back()) {
            if (row.monitor >= blocks_.size()) blocks_.resize(std::size_t{row.monitor} + 1);
            blocks_[row.monitor].push_back({index, 0, row.hour, row.hour});
            startNewBlock_ = false;
        }

        Block& block = blocks_[row.monitor].back();
        if (block.count > 0 && row.hour <= block.last) ++orderViolations_;  // Q1 assumes sorted blocks
        block.first = std::min(block.first, row.hour);
        block.last = std::max(block.last, row.hour);
        ++block.count;

        segment.minHour = std::min(segment.minHour, row.hour);
        segment.maxHour = std::max(segment.maxHour, row.hour);

        hours_.push_back(row.hour);
        monitors_.push_back(row.monitor);
        values_.push_back(row.value);
        qualifiers_.push_back(row.qualifier);
        methods_.push_back(row.method);
    }
    segment.end = static_cast<std::uint32_t>(values_.size());
}

void ColumnStore::endSegment() {
    if (!segmentOpen_) return;
    segmentOpen_ = false;
    if (segments_.back().begin == segments_.back().end) segments_.pop_back();
}

std::size_t ColumnStore::memoryBytes() const {
    std::size_t bytes = hours_.capacity() * sizeof(HourIndex) + monitors_.capacity() * sizeof(MonitorId) +
                        values_.capacity() * sizeof(ScaledValue) + qualifiers_.capacity() * sizeof(CodeId) +
                        methods_.capacity() * sizeof(CodeId);
    bytes += segments_.capacity() * sizeof(Segment);
    bytes += blocks_.capacity() * sizeof(std::vector<Block>);
    for (const auto& list : blocks_) bytes += list.capacity() * sizeof(Block);
    return bytes;
}

// ---- Q1 --------------------------------------------------------------------

void ColumnStore::rangeByMonitor(MonitorId monitor, HourRange range, std::vector<ColumnSlice>& out) const {
    if (monitor >= blocks_.size()) return;
    for (const Block& block : blocks_[monitor]) {
        if (!range.overlaps(block.first, block.last)) continue;
        const HourIndex* first = hours_.data() + block.begin;
        const HourIndex* lo = std::lower_bound(first, first + block.count, range.begin);
        const HourIndex* hi = std::lower_bound(lo, first + block.count, range.end);
        if (lo == hi) continue;
        const auto start = static_cast<std::size_t>(lo - hours_.data());
        ColumnSlice slice;
        slice.monitor = monitor;
        slice.size = static_cast<std::size_t>(hi - lo);
        slice.hour = {hours_.data() + start, sizeof(HourIndex)};
        slice.value = {values_.data() + start, sizeof(ScaledValue)};
        slice.qualifier = {qualifiers_.data() + start, sizeof(CodeId)};
        slice.method = {methods_.data() + start, sizeof(CodeId)};
        out.push_back(slice);
    }
}

void ColumnStore::copyRangeByMonitor(MonitorId monitor, HourRange range, std::vector<Measurement>& out) const {
    if (monitor >= blocks_.size()) return;
    for (const Block& block : blocks_[monitor]) {
        if (!range.overlaps(block.first, block.last)) continue;
        const HourIndex* first = hours_.data() + block.begin;
        const HourIndex* lo = std::lower_bound(first, first + block.count, range.begin);
        const HourIndex* hi = std::lower_bound(lo, first + block.count, range.end);
        const auto begin = static_cast<std::size_t>(lo - hours_.data());
        const auto end = static_cast<std::size_t>(hi - hours_.data());
        out.reserve(out.size() + (end - begin));  // one allocation instead of repeated growth
        for (std::size_t i = begin; i < end; ++i) out.push_back(rowAt(i));
    }
}

std::size_t ColumnStore::rangeByMonitorScan(MonitorId monitor, HourRange range) const {
    std::size_t count = 0;
    const std::size_t n = monitors_.size();
    for (std::size_t i = 0; i < n; ++i) {
        if (monitors_[i] == monitor && range.contains(hours_[i])) ++count;
    }
    return count;
}

// ---- Q2 --------------------------------------------------------------------

template <typename OnMatch>
void ColumnStore::scanValues(const ValueQuery& query, OnMatch&& onMatch) const {
    const ScaledValue lo = query.lo;
    const ScaledValue hi = query.hi;
    const ScaledValue* values = values_.data();
    for (const Segment& segment : segments_) {
        if (segment.pollutant != query.pollutant) continue;
        if (!query.range.overlaps(segment.minHour, segment.maxHour)) continue;

        if (query.range.covers(segment.minHour, segment.maxHour)) {
            // Only the value column is read.
            for (std::uint32_t i = segment.begin; i < segment.end; ++i) {
                if (values[i] >= lo && values[i] <= hi) onMatch(i);
            }
        } else {
            for (std::uint32_t i = segment.begin; i < segment.end; ++i) {
                if (values[i] >= lo && values[i] <= hi && query.range.contains(hours_[i])) onMatch(i);
            }
        }
    }
}

std::size_t ColumnStore::countByValue(const ValueQuery& query) const {
    const ScaledValue lo = query.lo;
    const ScaledValue hi = query.hi;
    const ScaledValue* values = values_.data();
    std::size_t count = 0;
    for (const Segment& segment : segments_) {
        if (segment.pollutant != query.pollutant) continue;
        if (!query.range.overlaps(segment.minHour, segment.maxHour)) continue;
        if (query.range.covers(segment.minHour, segment.maxHour)) {
            // Branch-free over a dense array of int16: the compiler can vectorize this loop.
            std::size_t local = 0;
            for (std::uint32_t i = segment.begin; i < segment.end; ++i) {
                local += static_cast<std::size_t>((values[i] >= lo) & (values[i] <= hi));
            }
            count += local;
        } else {
            for (std::uint32_t i = segment.begin; i < segment.end; ++i) {
                if (values[i] >= lo && values[i] <= hi && query.range.contains(hours_[i])) ++count;
            }
        }
    }
    return count;
}

void ColumnStore::selectByValue(const ValueQuery& query, std::vector<Measurement>& out) const {
    scanValues(query, [&](std::uint32_t i) { out.push_back(rowAt(i)); });
}

void ColumnStore::forEachByValue(const ValueQuery& query, ResultSink& sink) const {
    constexpr std::size_t kBatch = 4096;
    std::vector<Measurement> batch;
    batch.reserve(kBatch);
    scanValues(query, [&](std::uint32_t i) {
        batch.push_back(rowAt(i));
        if (batch.size() == kBatch) {
            sink.onBatch(batch);
            batch.clear();
        }
    });
    if (!batch.empty()) sink.onBatch(batch);
}

std::size_t ColumnStore::countMatching(Pollutant pollutant, const RowPredicate& predicate) const {
    std::size_t count = 0;
    for (const Segment& segment : segments_) {
        if (segment.pollutant != pollutant) continue;
        for (std::uint32_t i = segment.begin; i < segment.end; ++i) {
            if (predicate.matches(rowAt(i))) ++count;  // each row is rebuilt to cross the virtual call
        }
    }
    return count;
}

// ---- Q3 --------------------------------------------------------------------

template <GroupBy G>
void ColumnStore::aggregateInto(const AggregateQuery& query, std::vector<Stats>& groups) const {
    for (const Segment& segment : segments_) {
        if (segment.pollutant != query.pollutant) continue;
        if (!query.range.overlaps(segment.minHour, segment.maxHour)) continue;

        const bool whole = query.range.covers(segment.minHour, segment.maxHour);
        for (std::uint32_t i = segment.begin; i < segment.end; ++i) {
            if (!whole && !query.range.contains(hours_[i])) continue;
            if constexpr (G == GroupBy::None) {
                groups[0].add(values_[i]);
            } else if constexpr (G == GroupBy::Monitor) {
                groups[monitors_[i]].add(values_[i]);
            } else {
                groups[hours_[i] % 24].add(values_[i]);  // hour index starts at midnight
            }
        }
    }
}

AggregateResult ColumnStore::aggregate(const AggregateQuery& query, std::size_t monitorCount) const {
    AggregateResult result;
    result.pollutant = query.pollutant;
    result.groupBy = query.groupBy;
    switch (query.groupBy) {
        case GroupBy::None:
            result.groups.resize(1);
            aggregateInto<GroupBy::None>(query, result.groups);
            break;
        case GroupBy::Monitor:
            result.groups.resize(monitorCount);
            aggregateInto<GroupBy::Monitor>(query, result.groups);
            break;
        case GroupBy::HourOfDay:
            result.groups.resize(24);
            aggregateInto<GroupBy::HourOfDay>(query, result.groups);
            break;
    }
    return result;
}

}  // namespace aq
