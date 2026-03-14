add_library(spectre-window
    src/sdl_window.cpp
)
target_include_directories(spectre-window PUBLIC include)
target_link_libraries(spectre-window PUBLIC spectre-types SDL3::SDL3)

if(APPLE)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
    target_link_libraries(spectre-window PRIVATE ${COREGRAPHICS_FRAMEWORK})
elseif(WIN32)
    target_link_libraries(spectre-window PRIVATE Shcore)
endif()
