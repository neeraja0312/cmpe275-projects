#include "CsvTokenizer.hpp"

namespace aq {

std::size_t CsvTokenizer::split(std::string_view line, Fields& out) noexcept {
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

    constexpr auto npos = std::string_view::npos;
    const std::size_t size = line.size();
    // Fields are built with the (pointer, length) constructor rather than
    // substr(), which can throw; every offset below is <= size.
    const auto slice = [&line](std::size_t from, std::size_t to) {
        return std::string_view(line.data() + from, to - from);
    };

    std::size_t count = 0;
    std::size_t pos = 0;
    while (true) {
        if (count == kMaxFields) return kMaxFields + 1;

        std::size_t afterField = 0;
        if (pos < size && line[pos] == '"') {
            const std::size_t close = line.find('"', pos + 1);
            afterField = close == npos ? size : close + 1;  // unterminated quote: take the rest
            out[count++] = slice(pos + 1, close == npos ? size : close);
        } else {
            const std::size_t comma = line.find(',', pos);
            afterField = comma == npos ? size : comma;
            out[count++] = slice(pos, afterField);
        }

        const std::size_t comma = line.find(',', afterField);
        if (comma == npos) break;
        pos = comma + 1;
    }
    return count;
}

}  // namespace aq
