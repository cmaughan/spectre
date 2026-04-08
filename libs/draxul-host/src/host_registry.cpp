#include <draxul/host_registry.h>

#include <draxul/host.h>
#include <draxul/perf_timing.h>

namespace draxul
{

void HostProviderRegistry::register_provider(HostKind kind, HostFactory factory)
{
    PERF_MEASURE();
    if (!factory)
        return;
    for (auto& entry : providers_)
    {
        if (entry.kind == kind)
        {
            entry.factory = std::move(factory);
            return;
        }
    }
    providers_.push_back(Entry{ kind, std::move(factory) });
}

bool HostProviderRegistry::has(HostKind kind) const
{
    for (const auto& entry : providers_)
    {
        if (entry.kind == kind)
            return true;
    }
    return false;
}

std::unique_ptr<IHost> HostProviderRegistry::create(HostKind kind) const
{
    PERF_MEASURE();
    for (const auto& entry : providers_)
    {
        if (entry.kind == kind && entry.factory)
            return entry.factory();
    }
    return nullptr;
}

void HostProviderRegistry::clear()
{
    providers_.clear();
}

HostProviderRegistry& HostProviderRegistry::global()
{
    static HostProviderRegistry instance;
    return instance;
}

} // namespace draxul
