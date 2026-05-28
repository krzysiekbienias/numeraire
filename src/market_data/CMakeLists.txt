# Market data providers and snapshots (Sprint 8).
add_library(numeraire_market_data STATIC
        static_market_data_provider.cpp
        vol_surface_interpolation.cpp
        sqlite_vol_surface_market_data.cpp
)

target_include_directories(numeraire_market_data
        PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(numeraire_market_data
        PUBLIC
        numeraire_core
        numeraire_database
        numeraire_schedule
        PRIVATE
        numeraire_utils
)
