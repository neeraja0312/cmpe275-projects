#include "MonitorRegistry.hpp"

#include <limits>
#include <stdexcept>

namespace aq {

std::optional<MonitorId> MonitorRegistry::find(const MonitorKey& key) const {
    const auto it = index_.find(key.packed());
    if (it == index_.end()) return std::nullopt;
    return it->second;
}

std::pair<MonitorId, bool> MonitorRegistry::findOrAdd(const MonitorKey& key) {
    const auto [it, added] = index_.try_emplace(key.packed(), MonitorId{0});
    if (!added) return {it->second, false};

    if (monitors_.size() > std::numeric_limits<MonitorId>::max()) {
        index_.erase(it);
        throw std::overflow_error("MonitorRegistry: more monitors than MonitorId can index");
    }
    const auto id = static_cast<MonitorId>(monitors_.size());
    it->second = id;
    Monitor monitor;
    monitor.key = key;
    monitors_.push_back(std::move(monitor));
    return {id, true};
}

}  // namespace aq
