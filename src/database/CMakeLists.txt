add_library(numeraire_database STATIC
        sqlite_trade_repository.cpp
)

target_include_directories(numeraire_database
        PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(numeraire_database
        PUBLIC
        numeraire_enums
        numeraire_utils
        PRIVATE
        SQLiteCpp
)
