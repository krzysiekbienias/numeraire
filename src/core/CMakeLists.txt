# Core domain — abstractions (header-only until implementation TUs ship).
add_library(numeraire_core INTERFACE)

target_include_directories(numeraire_core
        INTERFACE
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(numeraire_core
        INTERFACE
        numeraire_enums
)
