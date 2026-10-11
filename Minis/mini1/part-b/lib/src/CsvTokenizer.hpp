#pragma once

// Splits one CSV line into fields without copying: each field is a
// string_view into the caller's line buffer.

#include <array>
#include <cstddef>
#include <string_view>

namespace aq {

class CsvTokenizer {
public:
    static constexpr std::size_t kMaxFields = 32;
    using Fields = std::array<std::string_view, kMaxFields>;

    // Removes the surrounding double quotes of quoted fields and keeps commas
    // inside quotes (EPA method names contain them). A trailing '\r' is ignored.
    // Returns the number of fields, or kMaxFields + 1 when the line has more
    // fields than fit; the caller treats that as a bad row.
    static std::size_t split(std::string_view line, Fields& out) noexcept;
};

}  // namespace aq
