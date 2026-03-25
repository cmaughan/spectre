# CMakeGraphVizOptions.cmake
# Controls output of `cmake --graphviz`. Has NO effect on the normal build.
# Read only when scripts/gen_deps.py (or cmake --graphviz directly) is run.

# Hide imported/external targets (SDL3, freetype, harfbuzz, Vulkan, VMA, etc.)
# These come from FetchContent or find_package and clutter the graph.
set(GRAPHVIZ_EXTERNAL_LIBS FALSE)

# Hide interface/header-only targets (e.g. Vulkan::Headers)
set(GRAPHVIZ_INTERFACE_LIBS FALSE)

# Skip the per-target .dot files cmake generates alongside the main one
set(GRAPHVIZ_GENERATE_PER_TARGET FALSE)
set(GRAPHVIZ_GENERATE_DEPENDERS FALSE)
