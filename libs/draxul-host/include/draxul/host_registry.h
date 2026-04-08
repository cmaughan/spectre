#pragma once

#include <draxul/host_kind.h>
#include <functional>
#include <memory>
#include <vector>

// HostProviderRegistry — narrow plugin-style boundary for host kinds.
//
// Built-in hosts (nvim, shell variants, nanovg demo) are registered at startup
// via register_builtin_host_providers(). Optional modules — currently MegaCity
// — register themselves through the same interface from the executable, so the
// core libraries (draxul-host, draxul-app) carry no headers, types, or symbols
// from those modules. A build with an optional module disabled simply has no
// provider for its HostKind, and create() returns nullptr.
//
// Intentionally tiny: this is a source-level boundary, not an ABI. We can
// extend it later (metadata, capability flags, dynamic loading) once the
// product shape calls for it.

namespace draxul
{

class IHost;

using HostFactory = std::function<std::unique_ptr<IHost>()>;

class HostProviderRegistry
{
public:
    void register_provider(HostKind kind, HostFactory factory);
    bool has(HostKind kind) const;
    std::unique_ptr<IHost> create(HostKind kind) const;
    void clear();

    static HostProviderRegistry& global();

private:
    struct Entry
    {
        HostKind kind;
        HostFactory factory;
    };
    std::vector<Entry> providers_;
};

// Registers the host kinds that ship with the core terminal product
// (Nvim, shell family, NanoVG demo). Optional modules are not registered here.
void register_builtin_host_providers(HostProviderRegistry& registry);

} // namespace draxul
