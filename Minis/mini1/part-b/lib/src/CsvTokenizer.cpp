#include "CsvTokenizer.hpp"

namespace aq {

std::size_t CsvTokenizer::split(std::string_view line, Fields& out) noexcept {
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

    const std::size_t size = line.size();
    std::size_t count = 0;
    std::size_t pos = 0;
    while (true) {
        if (count == kMaxFields) return kMaxFields + 1;

        std::size_t begin = pos;
        if (pos < size && line[pos] == '"') {
            begin = ++pos;
            while (pos < size && line[pos] != '"') ++pos;
            out[count++] = std::string_view(line.data() + begin, pos - begin);
            if (pos < size) ++pos;  // closing quote
            // EPA fields do not use escaped quotes. Ignore any text between a
            // closing quote and the delimiter, matching the original parser.
            while (pos < size && line[pos] != ',') ++pos;
        } else {
            while (pos < size && line[pos] != ',') ++pos;
            out[count++] = std::string_view(line.data() + begin, pos - begin);
        }

        if (pos == size) break;
        ++pos;  // comma
    }
    return count;
}

}  // namespace aq
