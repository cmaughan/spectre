add_library(draxul-ui
    src/ui_panel.cpp
)

target_include_directories(draxul-ui PUBLIC include)
target_link_libraries(draxul-ui PUBLIC draxul-types imgui PRIVATE SDL3::SDL3)
