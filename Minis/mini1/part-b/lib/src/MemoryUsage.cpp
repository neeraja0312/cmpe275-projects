#include "aqdata/Metrics.hpp"

#include <sys/resource.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#else
#include <cstdio>
#endif

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

#if defined(__APPLE__)

namespace {
bool taskVmInfo(task_vm_info_data_t& info) {
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    return task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS;
}
}  // namespace

std::size_t peakFootprintBytes() {
    task_vm_info_data_t info{};
    if (!taskVmInfo(info)) return peakRssBytes();
    // The peak ledger exists on newer macOS; never report less than the current footprint.
    const std::size_t peak = static_cast<std::size_t>(info.ledger_phys_footprint_peak);
    const std::size_t now = static_cast<std::size_t>(info.phys_footprint);
    return peak > now ? peak : now;
}

std::size_t currentFootprintBytes() {
    task_vm_info_data_t info{};
    if (!taskVmInfo(info)) return 0;
    return static_cast<std::size_t>(info.phys_footprint);
}

#else

std::size_t peakFootprintBytes() { return peakRssBytes(); }

std::size_t currentFootprintBytes() {
    std::FILE* f = std::fopen("/proc/self/statm", "r");
    if (f == nullptr) return 0;
    unsigned long size = 0, resident = 0;
    const int n = std::fscanf(f, "%lu %lu", &size, &resident);
    std::fclose(f);
    if (n != 2) return 0;
    return static_cast<std::size_t>(resident) * static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
}

#endif

}  // namespace aq
