# Persistence-facing DTOs and repositories (header-only until SQLite lands).
add_library(numeraire_database INTERFACE)

target_include_directories(numeraire_database
        INTERFACE
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(numeraire_database
        INTERFACE
        numeraire_enums
)
