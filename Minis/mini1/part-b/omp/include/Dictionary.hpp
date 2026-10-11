#pragma once

// Dictionary encoding (Flyweight): each distinct string is stored once and
// rows refer to it by a small integer id. Id 0 is always the empty string.

#include <cstddef>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace aq {

template <typename Id>
class Dictionary {
public:
    Dictionary() { ids_.emplace(std::string{}, Id{0}); }

    Id intern(std::string_view text) {
        // Consecutive rows usually repeat the same code, so check the last hit first.
        if (text == names_[last_]) return last_;
        if (const auto it = ids_.find(text); it != ids_.end()) {
            last_ = it->second;
            return last_;
        }
        if (names_.size() > std::numeric_limits<Id>::max()) {
            throw std::overflow_error("Dictionary: too many distinct values for the id type");
        }
        const auto id = static_cast<Id>(names_.size());
        names_.emplace_back(text);
        ids_.emplace(names_.back(), id);
        last_ = id;
        return id;
    }

    const std::string& name(Id id) const { return names_.at(id); }
    std::size_t size() const { return names_.size(); }

private:
    struct Hash {
        using is_transparent = void;
        std::size_t operator()(std::string_view s) const noexcept {
            return std::hash<std::string_view>{}(s);
        }
    };

    std::unordered_map<std::string, Id, Hash, std::equal_to<>> ids_;
    std::vector<std::string> names_{std::string{}};
    Id last_ = 0;
};

}  // namespace aq
