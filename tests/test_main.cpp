#define CATCH_CONFIG_RUNNER
#include <catch2/catch_all.hpp>

#include <draxul/host_registry.h>
#include <draxul/log.h>
#include <draxul/nanovg_demo_host.h>

#ifndef _WIN32
#include <csignal>
#endif
#ifdef DRAXUL_ENABLE_MEGACITY
#include <draxul/megacity_host.h>
#endif

int main(int argc, char* argv[])
{
#ifndef _WIN32
    // Ignore SIGPIPE so tests that spawn child processes (rpc-fake, shell hosts)
    // don't kill the entire test binary when a pipe breaks.
    signal(SIGPIPE, SIG_IGN);
#endif
    draxul::configure_logging();

    // Tests share the same provider registry as the app. Register the same
    // set the executable would so HostManager can create real hosts.
    auto& registry = draxul::HostProviderRegistry::global();
    registry.clear();
    draxul::register_builtin_host_providers(registry);
    draxul::register_nanovg_demo_host_provider(registry);
#ifdef DRAXUL_ENABLE_MEGACITY
    draxul::register_megacity_host_provider(registry);
#endif

    return Catch::Session().run(argc, argv);
}
