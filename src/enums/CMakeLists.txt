# Domain enumerations — header-only module (no translation units).
add_library(numeraire_enums INTERFACE)
target_include_directories(numeraire_enums INTERFACE ${CMAKE_SOURCE_DIR}/include)
