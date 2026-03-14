// Platform-specific renderer factory
// This file creates the correct renderer backend based on the platform.
// It's in the app target (not the library) because it needs to know
// which concrete renderer to instantiate.

#include <spectre/renderer.h>
#include <memory>

#ifdef __APPLE__
// Forward declare - the header is internal to spectre-renderer
namespace spectre { class MetalRenderer; }
#include "../libs/spectre-renderer/src/metal/metal_renderer.h"
#else
#include "../libs/spectre-renderer/src/vulkan/vk_renderer.h"
#endif

namespace spectre {

std::unique_ptr<IRenderer> create_renderer() {
#ifdef __APPLE__
    return std::make_unique<MetalRenderer>();
#else
    return std::make_unique<VkRenderer>();
#endif
}

} // namespace spectre
