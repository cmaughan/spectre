cmake_minimum_required(VERSION 3.25)

find_package(SQLite3 REQUIRED)

# CMake 4.x may find SQLite3 without creating the imported target.
# Create it manually when missing so that target_link_libraries succeeds.
if(NOT TARGET SQLite3::SQLite3)
    add_library(SQLite3::SQLite3 UNKNOWN IMPORTED)
    set_target_properties(SQLite3::SQLite3 PROPERTIES
        IMPORTED_LOCATION "${SQLite3_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${SQLite3_INCLUDE_DIR}"
    )
endif()

add_library(draxul-citydb STATIC
    src/citydb.cpp
)

target_include_directories(draxul-citydb
    PUBLIC include
    PRIVATE src
)

target_link_libraries(draxul-citydb PUBLIC
    draxul-treesitter
    SQLite3::SQLite3
)
