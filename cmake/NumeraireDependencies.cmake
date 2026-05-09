# Numeraire++ — external dependencies
#
# Single source of truth for every third-party library used by the project.
# Per-module CMakeLists.txt then link only the deps they actually need.
#
# Adding a new library? Drop the find_package() here, then in your module:
#   target_link_libraries(numeraire_<module> PUBLIC|PRIVATE <imported-target>)

# --- Core deps (used by Sprint 1+) ------------------------------------------
find_package(spdlog        CONFIG REQUIRED)   # logging
find_package(fmt           CONFIG REQUIRED)   # used by spdlog and directly
find_package(nlohmann_json CONFIG REQUIRED)   # JSON config + payloads

# --- SQLite (used by Stage 2+ trade repository) -----------------------------
find_package(SQLite3              REQUIRED)

# --- QuantLib ---------------------------------------------------------------
# Homebrew currently ships QuantLib without a CMake config (as of 1.37), so we
# fall back to find_library + manual IMPORTED target. When QL adds a config we
# can drop the fallback branch.
find_package(QuantLib CONFIG QUIET)
if(NOT TARGET QuantLib::QuantLib)
    find_library(NUMERAIRE_QUANTLIB_LIBRARY
            NAMES QuantLib
            HINTS /opt/homebrew/lib /usr/local/lib
            REQUIRED
    )
    find_path(NUMERAIRE_QUANTLIB_INCLUDE_DIR
            NAMES ql/quantlib.hpp
            HINTS /opt/homebrew/include /usr/local/include
            REQUIRED
    )
    add_library(QuantLib::QuantLib UNKNOWN IMPORTED)
    set_target_properties(QuantLib::QuantLib PROPERTIES
            IMPORTED_LOCATION             "${NUMERAIRE_QUANTLIB_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${NUMERAIRE_QUANTLIB_INCLUDE_DIR}"
    )
    message(STATUS "QuantLib (manual): ${NUMERAIRE_QUANTLIB_LIBRARY}")
endif()

# --- Test framework (only when building tests) ------------------------------
if(NUMERAIRE_BUILD_TESTS)
    find_package(GTest CONFIG REQUIRED)
endif()

# --- Stage 3+ dependencies (uncomment when modules ship) --------------------
# find_package(cpr     CONFIG REQUIRED)   # HTTP client for Polygon.io
# find_package(pugixml CONFIG REQUIRED)   # XML parsing if needed
