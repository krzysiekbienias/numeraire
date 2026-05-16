# Optional HTTP ingest (Polygon / Massive, …) — linked from dev_main or future jobs.
include(FetchContent)
set(CPR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CPR_BUILD_CLI OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
        cpr
        GIT_REPOSITORY https://github.com/libcpr/cpr.git
        GIT_TAG 1.11.3
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(cpr)
if(TARGET cpr)
    # libcurl public headers use old-style casts; project warnings would otherwise fail the CPR build.
    target_compile_options(cpr PRIVATE -Wno-old-style-cast)
endif()

add_library(numeraire_market_data_providers STATIC
        polygon_daily_eod_fetch.cpp
)

target_include_directories(numeraire_market_data_providers
        PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(numeraire_market_data_providers
        PUBLIC
        numeraire_database
        numeraire_utils
        PRIVATE
        SQLiteCpp
        cpr::cpr
)
