#pragma once

// One entry per monitor (instrument). Rows carry only a 2-byte MonitorId;
// codes, location and names are stored here once.

#include "aqdata/Types.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace aq {

class MonitorRegistry {
public:
    std::optional<MonitorId> find(const MonitorKey& key) const;

    // Returns the monitor's id and whether it was added by this call.
    std::pair<MonitorId, bool> findOrAdd(const MonitorKey& key);

    Monitor& at(MonitorId id) { return monitors_.at(id); }
    const Monitor& at(MonitorId id) const { return monitors_.at(id); }
    std::size_t size() const { return monitors_.size(); }
    const std::vector<Monitor>& all() const { return monitors_; }

private:
    std::unordered_map<std::uint64_t, MonitorId> index_;
    std::vector<Monitor> monitors_;
};

}  // namespace aq
