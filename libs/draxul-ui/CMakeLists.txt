add_library(draxul-ui
    src/sdl_imgui_input.cpp
    src/ui_metrics_panel.cpp
    src/ui_panel.cpp
    src/ui_panel_style.cpp
)

target_include_directories(draxul-ui PUBLIC include)
target_link_libraries(draxul-ui PUBLIC draxul-types imgui PRIVATE SDL3::SDL3 draxul-renderer)
