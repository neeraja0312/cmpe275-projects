#include "AosStore.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace aq {

void AosStore::reserve(std::size_t rows) { rows_.reserve(rows); }

void AosStore::beginSegment(Pollutant pollutant) {
    if (segmentOpen_) endSegment();
    const auto start = static_cast<std::uint32_t>(rows_.size());
    segments_.push_back({pollutant, start, start, std::numeric_limits<HourIndex>::max(), 0});
    segmentOpen_ = true;
    startNewBlock_ = true;
}

void AosStore::appendBatch(std::span<const Measurement> batch) {
    if (!segmentOpen_) throw std::logic_error("AosStore::appendBatch called outside a segment");
    if (rows_.size() + batch.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::overflow_error("AosStore: more rows than a 32-bit row index can address");
    }

    Segment& segment = segments_.back();
    for (const Measurement& row : batch) {
        const auto index = static_cast<std::uint32_t>(rows_.size());
        if (startNewBlock_ || row.monitor != rows_.back().monitor) {
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
        rows_.push_back(row);
    }
    segment.end = static_cast<std::uint32_t>(rows_.size());
}

void AosStore::endSegment() {
    if (!segmentOpen_) return;
    segmentOpen_ = false;
    if (segments_.back().begin == segments_.back().end) segments_.pop_back();
}

std::size_t AosStore::memoryBytes() const {
    std::size_t bytes = rows_.capacity() * sizeof(Measurement);
    bytes += segments_.capacity() * sizeof(Segment);
    bytes += blocks_.capacity() * sizeof(std::vector<Block>);
    for (const auto& list : blocks_) bytes += list.capacity() * sizeof(Block);
    return bytes;
}

// ---- Q1 --------------------------------------------------------------------

namespace {
// Narrows a block's rows to [range) with two binary searches.
std::pair<const Measurement*, const Measurement*> narrow(const Measurement* first, const Measurement* last,
                                                          HourRange range) {
    const auto beforeHour = [](const Measurement& m, HourIndex h) { return m.hour < h; };
    const Measurement* lo = std::lower_bound(first, last, range.begin, beforeHour);
    const Measurement* hi = std::lower_bound(lo, last, range.end, beforeHour);
    return {lo, hi};
}
}  // namespace

void AosStore::rangeByMonitor(MonitorId monitor, HourRange range, std::vector<ColumnSlice>& out) const {
    if (monitor >= blocks_.size()) return;
    for (const Block& block : blocks_[monitor]) {
        if (!range.overlaps(block.first, block.last)) continue;
        const Measurement* first = rows_.data() + block.begin;
        const auto [lo, hi] = narrow(first, first + block.count, range);
        if (lo == hi) continue;
        // Every column is read with a 12-byte stride through the row array.
        ColumnSlice slice;
        slice.monitor = monitor;
        slice.size = static_cast<std::size_t>(hi - lo);
        slice.hour = {&lo->hour, sizeof(Measurement)};
        slice.value = {&lo->value, sizeof(Measurement)};
        slice.qualifier = {&lo->qualifier, sizeof(Measurement)};
        slice.method = {&lo->method, sizeof(Measurement)};
        out.push_back(slice);
    }
}

void AosStore::copyRangeByMonitor(MonitorId monitor, HourRange range, std::vector<Measurement>& out) const {
    if (monitor >= blocks_.size()) return;
    for (const Block& block : blocks_[monitor]) {
        if (!range.overlaps(block.first, block.last)) continue;
        const Measurement* first = rows_.data() + block.begin;
        const auto [lo, hi] = narrow(first, first + block.count, range);
        out.insert(out.end(), lo, hi);
    }
}

std::size_t AosStore::rangeByMonitorScan(MonitorId monitor, HourRange range) const {
    std::size_t count = 0;
    for (const Measurement& row : rows_) {
        if (row.monitor == monitor && range.contains(row.hour)) ++count;
    }
    return count;
}

// ---- Q2 --------------------------------------------------------------------

template <typename OnMatch>
void AosStore::scanValues(const ValueQuery& query, OnMatch&& onMatch) const {
    const ScaledValue lo = query.lo;
    const ScaledValue hi = query.hi;
    for (const Segment& segment : segments_) {
        if (segment.pollutant != query.pollutant) continue;
        if (!query.range.overlaps(segment.minHour, segment.maxHour)) continue;

        const Measurement* it = rows_.data() + segment.begin;
        const Measurement* end = rows_.data() + segment.end;
        if (query.range.covers(segment.minHour, segment.maxHour)) {
            for (; it != end; ++it) {
                if (it->value >= lo && it->value <= hi) onMatch(*it);
            }
        } else {
            for (; it != end; ++it) {
                if (it->value >= lo && it->value <= hi && query.range.contains(it->hour)) onMatch(*it);
            }
        }
    }
}

std::size_t AosStore::countByValue(const ValueQuery& query) const {
    // Same single-unsigned-compare, 32-bit-counter loop as ColumnStore, so a
    // comparison between the layouts measures memory layout and not loop style.
    // Segments that only partly overlap the time range use the general scan.
    if (query.lo > query.hi) return 0;
    const auto base = static_cast<std::uint16_t>(query.lo);
    const auto span = static_cast<std::uint16_t>(static_cast<std::uint16_t>(query.hi) - base);
    constexpr std::size_t kBlock = std::size_t{1} << 16;

    std::size_t count = 0;
    for (const Segment& segment : segments_) {
        if (segment.pollutant != query.pollutant) continue;
        if (!query.range.overlaps(segment.minHour, segment.maxHour)) continue;

        const Measurement* it = rows_.data() + segment.begin;
        const Measurement* end = rows_.data() + segment.end;
        if (query.range.covers(segment.minHour, segment.maxHour)) {
            while (it != end) {
                const Measurement* blockEnd = static_cast<std::size_t>(end - it) > kBlock ? it + kBlock : end;
                std::uint32_t local = 0;
                for (; it != blockEnd; ++it) {
                    local += static_cast<std::uint16_t>(static_cast<std::uint16_t>(it->value) - base) <= span;
                }
                count += local;
            }
        } else {
            for (; it != end; ++it) {
                if (it->value >= query.lo && it->value <= query.hi && query.range.contains(it->hour)) ++count;
            }
        }
    }
    return count;
}

void AosStore::selectByValue(const ValueQuery& query, std::vector<Measurement>& out) const {
    scanValues(query, [&out](const Measurement& row) { out.push_back(row); });
}

void AosStore::forEachByValue(const ValueQuery& query, ResultSink& sink) const {
    constexpr std::size_t kBatch = 4096;
    std::vector<Measurement> batch;
    batch.reserve(kBatch);
    scanValues(query, [&](const Measurement& row) {
        batch.push_back(row);
        if (batch.size() == kBatch) {
            sink.onBatch(batch);
            batch.clear();
        }
    });
    if (!batch.empty()) sink.onBatch(batch);
}

std::size_t AosStore::countMatching(Pollutant pollutant, const RowPredicate& predicate) const {
    std::size_t count = 0;
    for (const Segment& segment : segments_) {
        if (segment.pollutant != pollutant) continue;
        for (std::uint32_t i = segment.begin; i < segment.end; ++i) {
            if (predicate.matches(rows_[i])) ++count;
        }
    }
    return count;
}

// ---- Q3 --------------------------------------------------------------------

template <GroupBy G>
void AosStore::aggregateInto(const AggregateQuery& query, std::vector<Stats>& groups) const {
    for (const Segment& segment : segments_) {
        if (segment.pollutant != query.pollutant) continue;
        if (!query.range.overlaps(segment.minHour, segment.maxHour)) continue;

        const bool whole = query.range.covers(segment.minHour, segment.maxHour);
        const Measurement* end = rows_.data() + segment.end;
        for (const Measurement* it = rows_.data() + segment.begin; it != end; ++it) {
            if (!whole && !query.range.contains(it->hour)) continue;
            if constexpr (G == GroupBy::None) {
                groups[0].add(it->value);
            } else if constexpr (G == GroupBy::Monitor) {
                groups[it->monitor].add(it->value);
            } else {
                groups[it->hour % 24].add(it->value);  // hour index starts at midnight
            }
        }
    }
}

AggregateResult AosStore::aggregate(const AggregateQuery& query, std::size_t monitorCount) const {
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
