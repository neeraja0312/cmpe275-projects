#include "AirQualityDB.hpp"

#include <stdexcept>

namespace aq {

AirQualityDB::AirQualityDB(StoreKind kind) : store_(makeStore(kind)) {
    if (!store_) throw std::invalid_argument("unknown store kind");
}

LoadReport AirQualityDB::load(const std::filesystem::path& path, const LoadOptions& options) {
    DataLoader loader(registry_, qualifiers_, methods_, *store_);
    return loader.load(path, options);
}

std::optional<MonitorId> AirQualityDB::findMonitor(const MonitorKey& key) const { return registry_.find(key); }

std::vector<Measurement> AirQualityDB::rangeByMonitor(MonitorId monitor, HourRange range) const {
    const auto views = rangeByMonitorView(monitor, range);
    std::size_t total = 0;
    for (const auto& view : views) total += view.size();
    std::vector<Measurement> rows;
    rows.reserve(total);
    for (const auto& view : views) rows.insert(rows.end(), view.begin(), view.end());
    return rows;
}

std::vector<std::span<const Measurement>> AirQualityDB::rangeByMonitorView(MonitorId monitor,
                                                                           HourRange range) const {
    std::vector<std::span<const Measurement>> views;
    store_->rangeByMonitor(monitor, range, views);
    return views;
}

std::size_t AirQualityDB::rangeByMonitorScan(MonitorId monitor, HourRange range) const {
    return store_->rangeByMonitorScan(monitor, range);
}

std::size_t AirQualityDB::countByValue(const ValueQuery& query) const { return store_->countByValue(query); }

std::vector<Measurement> AirQualityDB::selectByValue(const ValueQuery& query) const {
    std::vector<Measurement> rows;
    store_->selectByValue(query, rows);
    return rows;
}

void AirQualityDB::forEachByValue(const ValueQuery& query, ResultSink& sink) const {
    store_->forEachByValue(query, sink);
}

std::size_t AirQualityDB::countByValueVirtual(const ValueQuery& query) const {
    const ValueRangePredicate predicate(query);
    return store_->countMatching(query.pollutant, predicate);
}

AggregateResult AirQualityDB::aggregate(const AggregateQuery& query) const {
    return store_->aggregate(query, registry_.size());
}

std::vector<MonitorId> AirQualityDB::monitorsInState(unsigned stateCode, std::optional<Pollutant> pollutant) const {
    std::vector<MonitorId> ids;
    const auto& monitors = registry_.all();
    for (std::size_t i = 0; i < monitors.size(); ++i) {
        const MonitorKey& key = monitors[i].key;
        if (key.state == stateCode && (!pollutant || key.pollutant == *pollutant)) {
            ids.push_back(static_cast<MonitorId>(i));
        }
    }
    return ids;
}

std::vector<MonitorId> AirQualityDB::monitorsInBox(double latMin, double latMax, double lonMin, double lonMax,
                                                   std::optional<Pollutant> pollutant) const {
    std::vector<MonitorId> ids;
    const auto& monitors = registry_.all();
    for (std::size_t i = 0; i < monitors.size(); ++i) {
        const Monitor& m = monitors[i];
        if (m.latitude >= latMin && m.latitude <= latMax && m.longitude >= lonMin && m.longitude <= lonMax &&
            (!pollutant || m.key.pollutant == *pollutant)) {
            ids.push_back(static_cast<MonitorId>(i));
        }
    }
    return ids;
}

std::uint64_t AirQualityDB::rowCount(Pollutant pollutant) const {
    std::uint64_t rows = 0;
    for (const Monitor& m : registry_.all()) {
        if (m.key.pollutant == pollutant) rows += m.rows;
    }
    return rows;
}

}  // namespace aq
