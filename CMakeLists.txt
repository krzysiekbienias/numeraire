cmake_minimum_required(VERSION 3.20)
project(numeraire
        LANGUAGES CXX
        VERSION   0.1.0
        DESCRIPTION "Numeraire++ — modular C++ derivative pricing framework"
)

# ---------------------------------------------------------------------------
# Project options
# ---------------------------------------------------------------------------
option(NUMERAIRE_BUILD_TESTS    "Build unit + integration tests"           ON)
option(NUMERAIRE_BUILD_APP      "Build the production app entry point"     ON)
option(NUMERAIRE_BUILD_DEV_MAIN "Build the dev_main sandbox entry point"   ON)

# ---------------------------------------------------------------------------
# Compile options + external dependencies
# ---------------------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(NumeraireCompileOptions)
include(NumeraireDependencies)

# ---------------------------------------------------------------------------
# Modular libraries (gated; activated as each module ships its CMakeLists.txt)
# ---------------------------------------------------------------------------
foreach(_module utils schedule core database market_data)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/${_module}/CMakeLists.txt)
        add_subdirectory(src/${_module})
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Executables
# ---------------------------------------------------------------------------
if(NUMERAIRE_BUILD_APP AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/app/main.cpp)
    add_executable(app app/main.cpp)
    target_link_libraries(app PRIVATE numeraire_utils)
endif()

if(NUMERAIRE_BUILD_DEV_MAIN AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/app/dev_main.cpp)
    add_executable(dev_main app/dev_main.cpp)
    target_link_libraries(dev_main PRIVATE numeraire_utils)
    target_compile_definitions(dev_main PRIVATE DEV_MAIN_BUILD=1)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(NUMERAIRE_BUILD_TESTS)
    enable_testing()
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit_tests/CMakeLists.txt)
        add_subdirectory(unit_tests)
    endif()
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/integration_tests/CMakeLists.txt)
        add_subdirectory(integration_tests)
    endif()
endif()

# ---------------------------------------------------------------------------
# Status summary
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "Numeraire++ ${PROJECT_VERSION} configured")
message(STATUS "  Build type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Tests        : ${NUMERAIRE_BUILD_TESTS}")
message(STATUS "  App          : ${NUMERAIRE_BUILD_APP}")
message(STATUS "  Dev main     : ${NUMERAIRE_BUILD_DEV_MAIN}")
message(STATUS "")
