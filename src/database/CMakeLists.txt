# Persistence-facing DTOs and repositories (ITradeRepository header; SQLite impl TBD).
add_library(numeraire_database INTERFACE)

target_include_directories(numeraire_database
        INTERFACE
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(numeraire_database
        INTERFACE
        numeraire_enums
)
