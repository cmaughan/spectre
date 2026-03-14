add_library(spectre-font
    src/font_manager.cpp
    src/glyph_cache.cpp
    src/text_shaper.cpp
)
target_include_directories(spectre-font PUBLIC include)
target_link_libraries(spectre-font PUBLIC spectre-types freetype harfbuzz)
