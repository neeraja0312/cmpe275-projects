#pragma once

// Allocation-free field parsers used on every row. Header-only so the compiler
// can inline them into the loader's hot loop.

#include <charconv>
#include <cstdint>
#include <string_view>
#include <system_error>

namespace aq::parse {

// Whole-field unsigned integer ("0023" -> 23). Rejects empty, signs and trailing junk.
template <typename T>
inline bool unsignedInt(std::string_view s, T& out) {
    if (s.empty()) return false;
    const char* end = s.data() + s.size();
    // from_chars takes an explicit end pointer, so s need not be null-terminated.
    const auto [ptr, ec] = std::from_chars(s.data(), end, out);  // NOLINT(bugprone-suspicious-stringview-data-usage)
    return ec == std::errc{} && ptr == end;
}

inline bool real(std::string_view s, double& out) {
    if (s.empty()) return false;
    const char* end = s.data() + s.size();
    const auto [ptr, ec] = std::from_chars(s.data(), end, out);  // NOLINT(bugprone-suspicious-stringview-data-usage)
    return ec == std::errc{} && ptr == end;
}

// Parses a decimal such as "-0.062" into an integer scaled by 10^decimals
// without going through floating point, so the result is exact.
// Extra fraction digits are rounded half away from zero.
inline bool scaled(std::string_view s, int decimals, std::int32_t& out) {
    constexpr std::int64_t kLimit = 1'000'000'000;
    std::size_t i = 0;
    bool negative = false;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) {
        negative = s[i] == '-';
        ++i;
    }

    std::int64_t whole = 0;
    int wholeDigits = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        whole = whole * 10 + (s[i] - '0');
        ++wholeDigits;
        ++i;
        if (whole > kLimit) return false;
    }

    std::int64_t frac = 0;
    int fracDigits = 0;  // all fraction digits seen
    int kept = 0;        // fraction digits kept (<= decimals)
    bool roundUp = false;
    if (i < s.size() && s[i] == '.') {
        ++i;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            if (kept < decimals) {
                frac = frac * 10 + (s[i] - '0');
                ++kept;
            } else if (fracDigits == decimals) {
                roundUp = s[i] >= '5';  // the first dropped digit decides
            }
            ++fracDigits;
            ++i;
        }
    }
    if (i != s.size() || (wholeDigits == 0 && fracDigits == 0)) return false;

    for (int d = kept; d < decimals; ++d) frac *= 10;
    std::int64_t pow10 = 1;
    for (int d = 0; d < decimals; ++d) pow10 *= 10;

    std::int64_t value = whole * pow10 + frac + (roundUp ? 1 : 0);
    if (negative) value = -value;
    if (value > kLimit || value < -kLimit) return false;
    out = static_cast<std::int32_t>(value);
    return true;
}

namespace detail {
inline bool digits(std::string_view s, std::size_t pos, std::size_t count, unsigned& out) {
    out = 0;
    for (std::size_t i = pos; i < pos + count; ++i) {
        const char c = s[i];
        if (c < '0' || c > '9') return false;
        out = out * 10 + static_cast<unsigned>(c - '0');
    }
    return true;
}
}  // namespace detail

// "YYYY-MM-DD"
inline bool date(std::string_view s, int& year, unsigned& month, unsigned& day) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
    unsigned y = 0;
    if (!detail::digits(s, 0, 4, y) || !detail::digits(s, 5, 2, month) ||
        !detail::digits(s, 8, 2, day)) {
        return false;
    }
    year = static_cast<int>(y);
    return month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

// "HH:00" -> HH. The data is hourly, so minutes must be 00.
inline bool hourOfDay(std::string_view s, unsigned& hour) {
    if (s.size() != 5 || s[2] != ':' || s[3] != '0' || s[4] != '0') return false;
    return detail::digits(s, 0, 2, hour) && hour < 24;
}

}  // namespace aq::parse
