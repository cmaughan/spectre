add_library(spectre-grid
    src/grid.cpp
)
target_include_directories(spectre-grid PUBLIC include)
target_link_libraries(spectre-grid PUBLIC spectre-types)
