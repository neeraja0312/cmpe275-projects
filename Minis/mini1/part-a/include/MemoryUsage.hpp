#pragma once

#include <cstddef>

namespace aq {

// Peak resident set size of this process so far, in bytes (getrusage).
std::size_t peakRssBytes();

}  // namespace aq
