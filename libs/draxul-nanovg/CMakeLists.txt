if(APPLE)
    add_library(draxul-nanovg
        src/nanovg_wrapper.c
        src/nanovg_mtl.mm
        src/nanovg_pass_metal.mm
    )
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    target_include_directories(draxul-nanovg
        PUBLIC include ${nanovg_SOURCE_DIR}/src
        PRIVATE src
        # Access metal_render_context.h (internal renderer header)
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../draxul-renderer/src
    )
    target_link_libraries(draxul-nanovg
        PUBLIC draxul-renderer
        PRIVATE ${METAL_FRAMEWORK} ${QUARTZCORE_FRAMEWORK}
    )
    # NanoVG Metal backend uses ARC for Objective-C memory management
    target_compile_options(draxul-nanovg PRIVATE -fobjc-arc)
else()
    add_library(draxul-nanovg
        src/nanovg_wrapper.c
        src/nanovg_vk.cpp
        src/nanovg_pass_vk.cpp
    )
    target_include_directories(draxul-nanovg
        PUBLIC include ${nanovg_SOURCE_DIR}/src
        PRIVATE src
        # Access vk_render_context.h (internal renderer header)
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../draxul-renderer/src
    )
    target_link_libraries(draxul-nanovg
        PUBLIC draxul-renderer
        PRIVATE Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator
    )
endif()

# Skip stb_image in nanovg.c — we only use NanoVG for vector shapes, not image
# loading. This avoids duplicate stb_image symbols with draxul-megacity.
target_compile_definitions(draxul-nanovg PRIVATE NVG_NO_STB)
