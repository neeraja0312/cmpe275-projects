#pragma once

// Small measurement helpers shared by the library and the benchmark driver.

#include "aqdata/Export.hpp"

#include <chrono>
#include <cstddef>

namespace aq {

class Stopwatch {
public:
    Stopwatch() : start_(std::chrono::steady_clock::now()) {}

    void reset() { start_ = std::chrono::steady_clock::now(); }

    double seconds() const {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    }

private:
    std::chrono::steady_clock::time_point start_;
};

// Peak resident set size so far, in bytes (getrusage). On macOS this misses
// memory the OS has compressed; prefer peakFootprintBytes() there.
AQ_API std::size_t peakRssBytes();

// Peak physical memory footprint so far, in bytes (macOS: task_info
// TASK_VM_INFO, the Activity Monitor figure; elsewhere falls back to peak RSS).
AQ_API std::size_t peakFootprintBytes();

// Current physical footprint in bytes (0 if unavailable).
AQ_API std::size_t currentFootprintBytes();

}  // namespace aq
