cmake_minimum_required(VERSION 3.25)
project(spectre VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_FLAGS "${CMAKE_OBJCXX_FLAGS} -fobjc-arc")
endif()

# Platform-specific GPU API
if(NOT APPLE)
    find_package(Vulkan REQUIRED COMPONENTS glslc)
endif()

# Fetch all dependencies
include(cmake/FetchDependencies.cmake)

# Compile shaders
if(APPLE)
    include(cmake/CompileShaders_Metal.cmake)
else()
    include(cmake/CompileShaders.cmake)
endif()

# --- Libraries ---
add_subdirectory(libs/spectre-types)
add_subdirectory(libs/spectre-window)
add_subdirectory(libs/spectre-renderer)
add_subdirectory(libs/spectre-font)
add_subdirectory(libs/spectre-grid)
add_subdirectory(libs/spectre-nvim)

# --- App executable ---
if(APPLE)
    add_executable(spectre
        app/main.cpp
        app/app.cpp
        app/renderer_factory.cpp
    )
else()
    add_executable(spectre
        app/main.cpp
        app/app.cpp
        app/renderer_factory.cpp
    )
endif()

target_include_directories(spectre PRIVATE app)
# The factory needs access to internal renderer headers
target_include_directories(spectre PRIVATE libs/spectre-renderer/src)

target_link_libraries(spectre PRIVATE
    spectre-types
    spectre-window
    spectre-renderer
    spectre-font
    spectre-grid
    spectre-nvim
)

if(APPLE)
    add_dependencies(spectre compile_metal_shaders)

    # Copy metallib to build dir next to executable
    add_custom_command(TARGET spectre POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:spectre>/shaders
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_BINARY_DIR}/shaders/grid.metallib
            $<TARGET_FILE_DIR:spectre>/shaders/
    )
else()
    add_dependencies(spectre compile_shaders)

    # Copy shaders to build dir next to executable
    add_custom_command(TARGET spectre POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_BINARY_DIR}/shaders
            $<TARGET_FILE_DIR:spectre>/shaders
    )
endif()

# Copy font to build directory
if(EXISTS ${CMAKE_SOURCE_DIR}/fonts/JetBrainsMonoNerdFontMono-Regular.ttf)
    add_custom_command(TARGET spectre POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:spectre>/fonts
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/fonts/JetBrainsMonoNerdFontMono-Regular.ttf
            $<TARGET_FILE_DIR:spectre>/fonts/
    )
endif()

if(WIN32)
    target_link_libraries(spectre PRIVATE ws2_32)
    set_target_properties(spectre PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
