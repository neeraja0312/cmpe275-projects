#include "MemoryUsage.hpp"

#include <sys/resource.h>

namespace aq {

std::size_t peakRssBytes() {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
    return static_cast<std::size_t>(usage.ru_maxrss);  // bytes on macOS
#else
    return static_cast<std::size_t>(usage.ru_maxrss) * 1024;  // kilobytes on Linux
#endif
}

}  // namespace aq
