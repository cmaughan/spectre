#include <draxul/system_resource_monitor.h>

#include <algorithm>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#else
#include <fstream>
#include <sstream>
#include <string>
#include <sys/sysinfo.h>
#endif

namespace draxul
{

namespace
{

constexpr auto kRefreshInterval = std::chrono::seconds(1);

int clamp_percent(double value)
{
    return std::clamp(static_cast<int>(std::lround(value)), 0, 100);
}

#ifdef _WIN32

uint64_t filetime_to_u64(const FILETIME& time)
{
    ULARGE_INTEGER value{};
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return value.QuadPart;
}

bool sample_cpu_times(SystemResourceMonitor::CpuTimes& times)
{
    FILETIME idle{};
    FILETIME kernel{};
    FILETIME user{};
    if (!GetSystemTimes(&idle, &kernel, &user))
        return false;

    times.idle = filetime_to_u64(idle);
    times.total = filetime_to_u64(kernel) + filetime_to_u64(user);
    times.valid = true;
    return true;
}

bool sample_memory_percent(int& memory_percent)
{
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status))
        return false;
    memory_percent = static_cast<int>(status.dwMemoryLoad);
    return true;
}

#elif defined(__APPLE__)

bool sample_cpu_times(SystemResourceMonitor::CpuTimes& times)
{
    host_cpu_load_info_data_t cpu{};
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, reinterpret_cast<host_info_t>(&cpu), &count)
        != KERN_SUCCESS)
        return false;

    times.idle = static_cast<uint64_t>(cpu.cpu_ticks[CPU_STATE_IDLE]);
    times.total = static_cast<uint64_t>(cpu.cpu_ticks[CPU_STATE_USER])
        + static_cast<uint64_t>(cpu.cpu_ticks[CPU_STATE_SYSTEM])
        + static_cast<uint64_t>(cpu.cpu_ticks[CPU_STATE_NICE])
        + times.idle;
    times.valid = true;
    return true;
}

bool sample_memory_percent(int& memory_percent)
{
    uint64_t total_bytes = 0;
    size_t total_size = sizeof(total_bytes);
    if (sysctlbyname("hw.memsize", &total_bytes, &total_size, nullptr, 0) != 0 || total_bytes == 0)
        return false;

    vm_statistics64_data_t vm{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm), &count)
        != KERN_SUCCESS)
        return false;

    vm_size_t page_size = 0;
    if (host_page_size(mach_host_self(), &page_size) != KERN_SUCCESS || page_size == 0)
        return false;

    const uint64_t used_pages = static_cast<uint64_t>(vm.active_count)
        + static_cast<uint64_t>(vm.wire_count)
        + static_cast<uint64_t>(vm.compressor_page_count);
    const double used_ratio = static_cast<double>(used_pages * static_cast<uint64_t>(page_size))
        / static_cast<double>(total_bytes);
    memory_percent = clamp_percent(used_ratio * 100.0);
    return true;
}

#else

bool sample_cpu_times(SystemResourceMonitor::CpuTimes& times)
{
    std::ifstream input("/proc/stat");
    std::string label;
    uint64_t user = 0;
    uint64_t nice = 0;
    uint64_t system = 0;
    uint64_t idle = 0;
    uint64_t iowait = 0;
    uint64_t irq = 0;
    uint64_t softirq = 0;
    uint64_t steal = 0;
    if (!(input >> label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal) || label != "cpu")
        return false;

    times.idle = idle + iowait;
    times.total = user + nice + system + idle + iowait + irq + softirq + steal;
    times.valid = true;
    return true;
}

bool sample_memory_percent(int& memory_percent)
{
    struct sysinfo info{};
    if (sysinfo(&info) != 0 || info.totalram == 0)
        return false;

    const uint64_t total = static_cast<uint64_t>(info.totalram) * info.mem_unit;
    const uint64_t free = static_cast<uint64_t>(info.freeram) * info.mem_unit;
    const double used_ratio = static_cast<double>(total - free) / static_cast<double>(total);
    memory_percent = clamp_percent(used_ratio * 100.0);
    return true;
}

#endif

} // namespace

bool SystemResourceMonitor::sample(SystemResourceSnapshot& snapshot, CpuTimes& times)
{
    if (!sample_cpu_times(times))
        return false;
    if (!sample_memory_percent(snapshot.memory_percent))
        return false;
    return true;
}

bool SystemResourceMonitor::refresh(std::chrono::steady_clock::time_point now)
{
    if (next_refresh_ && now < *next_refresh_)
        return false;

    SystemResourceSnapshot next_snapshot = snapshot_;
    CpuTimes current_times{};
    if (!sample(next_snapshot, current_times))
        return false;

    if (previous_cpu_times_.valid)
    {
        const uint64_t total_delta = current_times.total - previous_cpu_times_.total;
        const uint64_t idle_delta = current_times.idle - previous_cpu_times_.idle;
        if (total_delta > 0 && total_delta >= idle_delta)
        {
            const double busy_ratio = static_cast<double>(total_delta - idle_delta) / static_cast<double>(total_delta);
            next_snapshot.cpu_percent = clamp_percent(busy_ratio * 100.0);
        }
    }
    else
    {
        next_snapshot.cpu_percent = 0;
    }

    previous_cpu_times_ = current_times;
    next_refresh_ = now + kRefreshInterval;

    if (next_snapshot == snapshot_)
        return false;

    snapshot_ = next_snapshot;
    return true;
}

} // namespace draxul
