add_library(draxul-runtime-support
    src/cursor_blinker.cpp
    src/grid_rendering_pipeline.cpp
    src/ui_request_worker.cpp
)

target_include_directories(draxul-runtime-support PUBLIC
    include
)

target_link_libraries(draxul-runtime-support PUBLIC
    draxul-font
    draxul-grid
    draxul-nvim
    draxul-renderer
)
