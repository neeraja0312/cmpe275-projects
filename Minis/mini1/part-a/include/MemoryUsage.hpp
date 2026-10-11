#pragma once

#include <cstddef>

namespace aq {

// Peak resident set size of this process so far, in bytes (getrusage).
std::size_t peakRssBytes();

// Peak physical memory footprint of this process so far, in bytes.
// On macOS this is task_info(TASK_VM_INFO) ledger_phys_footprint_peak, the
// figure Activity Monitor shows. Unlike peakRssBytes() it includes memory the
// OS has compressed. On other systems it falls back to peakRssBytes().
std::size_t peakFootprintBytes();

// Current physical footprint in bytes (macOS: phys_footprint; elsewhere the
// current resident set size from /proc/self/statm, or 0 if unavailable).
std::size_t currentFootprintBytes();

}  // namespace aq
