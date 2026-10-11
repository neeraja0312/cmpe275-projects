#include "Types.hpp"

#include "Parse.hpp"

#include <array>
#include <cstdio>

namespace aq {

std::optional<Pollutant> pollutantFromCode(std::uint32_t code) {
    if (code == parameterCode(Pollutant::Ozone)) return Pollutant::Ozone;
    if (code == parameterCode(Pollutant::NO2)) return Pollutant::NO2;
    return std::nullopt;
}

std::optional<Pollutant> pollutantFromName(std::string_view name) {
    if (name == "ozone" || name == "o3" || name == "44201") return Pollutant::Ozone;
    if (name == "no2" || name == "42602") return Pollutant::NO2;
    return std::nullopt;
}

std::string toString(const MonitorKey& key) {
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%02u-%03u-%04u-%05u-%u", unsigned{key.state},
                  unsigned{key.county}, unsigned{key.site}, parameterCode(key.pollutant),
                  unsigned{key.poc});
    return buffer.data();
}

std::optional<MonitorKey> parseMonitorKey(std::string_view text) {
    std::array<std::string_view, 5> parts;
    std::size_t count = 0;
    std::size_t start = 0;
    while (true) {
        const std::size_t dash = text.find('-', start);
        if (count == parts.size()) return std::nullopt;  // too many parts
        parts[count++] = text.substr(start, dash == std::string_view::npos ? dash : dash - start);
        if (dash == std::string_view::npos) break;
        start = dash + 1;
    }
    if (count != parts.size()) return std::nullopt;

    MonitorKey key;
    std::uint32_t code = 0;
    if (!parse::unsignedInt(parts[0], key.state) || !parse::unsignedInt(parts[1], key.county) ||
        !parse::unsignedInt(parts[2], key.site) || !parse::unsignedInt(parts[3], code) ||
        !parse::unsignedInt(parts[4], key.poc)) {
        return std::nullopt;
    }
    const auto pollutant = pollutantFromCode(code);
    if (!pollutant) return std::nullopt;
    key.pollutant = *pollutant;
    return key;
}

std::string formatScaled(std::int32_t value, int decimals) {
    const bool negative = value < 0;
    const auto magnitude = static_cast<std::uint64_t>(negative ? -static_cast<std::int64_t>(value) : value);
    std::string text = std::to_string(magnitude);
    if (decimals > 0) {
        const auto places = static_cast<std::size_t>(decimals);
        if (text.size() <= places) text.insert(0, places + 1 - text.size(), '0');
        text.insert(text.size() - places, ".");
    }
    if (negative) text.insert(0, "-");
    return text;
}

}  // namespace aq
