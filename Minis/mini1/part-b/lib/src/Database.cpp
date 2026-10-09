#include "aqdata/Database.hpp"

#include "DataLoader.hpp"
#include "Dictionary.hpp"
#include "MeasurementStore.hpp"
#include "MonitorRegistry.hpp"

#include <stdexcept>

namespace aq {

// Everything the application must not see lives here.
struct Database::Impl {
    explicit Impl(Layout l) : layout(l), store(makeStore(l)) {
        if (!store) throw std::invalid_argument("unknown storage layout");
    }

    Layout layout;
    MonitorRegistry registry;
    Dictionary<CodeId> qualifiers;
    Dictionary<CodeId> methods;
    std::unique_ptr<MeasurementStore> store;
};

Database::Database(Layout layout) : impl_(std::make_unique<Impl>(layout)) {}
Database::~Database() = default;
Database::Database(Database&&) noexcept = default;
Database& Database::operator=(Database&&) noexcept = default;

LoadReport Database::load(const std::filesystem::path& path, const LoadOptions& options) {
    DataLoader loader(impl_->registry, impl_->qualifiers, impl_->methods, *impl_->store);
    return loader.load(path, options);
}

std::optional<MonitorId> Database::findMonitor(const MonitorKey& key) const { return impl_->registry.find(key); }

std::vector<Measurement> Database::rangeByMonitor(MonitorId monitor, HourRange range) const {
    std::vector<Measurement> rows;
    impl_->store->copyRangeByMonitor(monitor, range, rows);
    return rows;
}

std::vector<ColumnSlice> Database::rangeByMonitorView(MonitorId monitor, HourRange range) const {
    std::vector<ColumnSlice> slices;
    impl_->store->rangeByMonitor(monitor, range, slices);
    return slices;
}

std::size_t Database::rangeByMonitorScan(MonitorId monitor, HourRange range) const {
    return impl_->store->rangeByMonitorScan(monitor, range);
}

std::size_t Database::countByValue(const ValueQuery& query) const { return impl_->store->countByValue(query); }

std::vector<Measurement> Database::selectByValue(const ValueQuery& query) const {
    std::vector<Measurement> rows;
    impl_->store->selectByValue(query, rows);
    return rows;
}

void Database::forEachByValue(const ValueQuery& query, ResultSink& sink) const {
    impl_->store->forEachByValue(query, sink);
}

std::size_t Database::countByValueVirtual(const ValueQuery& query) const {
    const ValueRangePredicate predicate(query);
    return impl_->store->countMatching(query.pollutant, predicate);
}

AggregateResult Database::aggregate(const AggregateQuery& query) const {
    return impl_->store->aggregate(query, impl_->registry.size());
}

std::vector<MonitorId> Database::monitorsInState(unsigned stateCode, std::optional<Pollutant> pollutant) const {
    std::vector<MonitorId> ids;
    const auto& monitors = impl_->registry.all();
    for (std::size_t i = 0; i < monitors.size(); ++i) {
        const MonitorKey& key = monitors[i].key;
        if (key.state == stateCode && (!pollutant || key.pollutant == *pollutant)) {
            ids.push_back(static_cast<MonitorId>(i));
        }
    }
    return ids;
}

std::vector<MonitorId> Database::monitorsInBox(double latMin, double latMax, double lonMin, double lonMax,
                                               std::optional<Pollutant> pollutant) const {
    std::vector<MonitorId> ids;
    const auto& monitors = impl_->registry.all();
    for (std::size_t i = 0; i < monitors.size(); ++i) {
        const Monitor& m = monitors[i];
        if (m.latitude >= latMin && m.latitude <= latMax && m.longitude >= lonMin && m.longitude <= lonMax &&
            (!pollutant || m.key.pollutant == *pollutant)) {
            ids.push_back(static_cast<MonitorId>(i));
        }
    }
    return ids;
}

const Monitor& Database::monitor(MonitorId id) const { return impl_->registry.at(id); }
std::size_t Database::monitorCount() const { return impl_->registry.size(); }
std::size_t Database::rowCount() const { return impl_->store->size(); }

std::uint64_t Database::rowCount(Pollutant pollutant) const {
    std::uint64_t rows = 0;
    for (const Monitor& m : impl_->registry.all()) {
        if (m.key.pollutant == pollutant) rows += m.rows;
    }
    return rows;
}

std::size_t Database::storeBytes() const { return impl_->store->memoryBytes(); }
std::uint64_t Database::orderViolations() const { return impl_->store->orderViolations(); }
Layout Database::layout() const { return impl_->layout; }
std::string_view Database::storeName() const { return impl_->store->name(); }
const std::string& Database::qualifierName(CodeId id) const { return impl_->qualifiers.name(id); }
const std::string& Database::methodName(CodeId id) const { return impl_->methods.name(id); }
std::size_t Database::qualifierCount() const { return impl_->qualifiers.size(); }
std::size_t Database::methodCount() const { return impl_->methods.size(); }

}  // namespace aq
