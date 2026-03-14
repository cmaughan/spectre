if(APPLE)
    add_library(spectre-renderer
        src/metal/metal_renderer.mm
    )
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    target_link_libraries(spectre-renderer PRIVATE
        ${METAL_FRAMEWORK}
        ${QUARTZCORE_FRAMEWORK}
        ${FOUNDATION_FRAMEWORK}
    )
else()
    add_library(spectre-renderer
        src/vulkan/vk_renderer.cpp
        src/vulkan/vk_context.cpp
        src/vulkan/vk_pipeline.cpp
        src/vulkan/vk_buffers.cpp
        src/vulkan/vk_atlas.cpp
    )
    target_link_libraries(spectre-renderer PRIVATE
        Vulkan::Vulkan
        vk-bootstrap::vk-bootstrap
        GPUOpen::VulkanMemoryAllocator
    )
endif()

target_include_directories(spectre-renderer PUBLIC include)
target_include_directories(spectre-renderer PRIVATE src)
target_link_libraries(spectre-renderer PUBLIC spectre-types spectre-window SDL3::SDL3)
