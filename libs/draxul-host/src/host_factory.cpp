#include "nvim_host.h"

#include <draxul/host.h>
#include <draxul/host_registry.h>
#include <draxul/perf_timing.h>

namespace draxul
{
std::unique_ptr<IHost> create_shell_host();
#ifdef _WIN32
std::unique_ptr<IHost> create_powershell_host();
std::unique_ptr<IHost> create_wsl_host();
#endif
} // namespace draxul

namespace draxul
{

void register_builtin_host_providers(HostProviderRegistry& registry)
{
    PERF_MEASURE();
    registry.register_provider(HostKind::Nvim, [] {
        return std::unique_ptr<IHost>(std::make_unique<NvimHost>());
    });
    registry.register_provider(HostKind::Bash, [] { return create_shell_host(); });
    registry.register_provider(HostKind::Zsh, [] { return create_shell_host(); });
#ifdef _WIN32
    registry.register_provider(HostKind::PowerShell, [] { return create_powershell_host(); });
    registry.register_provider(HostKind::Wsl, [] { return create_wsl_host(); });
#endif
}

// Compatibility wrapper for callers that already had a HostKind in hand and
// just want a host. Newer callers should go through HostProviderRegistry.
std::unique_ptr<IHost> create_host(HostKind kind)
{
    return HostProviderRegistry::global().create(kind);
}

} // namespace draxul
