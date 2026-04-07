#pragma once

#include <chrono>
#include <optional>

namespace draxul
{

struct SystemResourceSnapshot
{
    int cpu_percent = -1;
    int memory_percent = -1;

    [[nodiscard]] bool available() const
    {
        return cpu_percent >= 0 && memory_percent >= 0;
    }

    [[nodiscard]] bool operator==(const SystemResourceSnapshot& other) const = default;
};

class SystemResourceMonitor
{
public:
    struct CpuTimes
    {
        uint64_t idle = 0;
        uint64_t total = 0;
        bool valid = false;
    };

    bool refresh(std::chrono::steady_clock::time_point now);

    [[nodiscard]] const SystemResourceSnapshot& snapshot() const
    {
        return snapshot_;
    }

private:
    static bool sample(SystemResourceSnapshot& snapshot, CpuTimes& times);

    SystemResourceSnapshot snapshot_{};
    CpuTimes previous_cpu_times_{};
    std::optional<std::chrono::steady_clock::time_point> next_refresh_{};
};

} // namespace draxul
