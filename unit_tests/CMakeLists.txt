# Unit tests for Numeraire++
#
# Discovers all test_*.cpp files anywhere under unit_tests/ (root or one of the
# per-module subdirectories: utils/, schedule/, core/, database/, market_data/).
# Sources are linked into a single test_environment binary so we get one fast
# build + one place to run filtered tests via --gtest_filter.

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/*/test_*.cpp"
)

if(TEST_SOURCES STREQUAL "")
        message(STATUS "unit_tests: no sources yet (add unit_tests/<module>/test_*.cpp).")
        return()
endif()

add_executable(test_environment ${TEST_SOURCES})
set_target_properties(test_environment PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Absolute path to repo root so tests can open committed files (e.g. configs/default.json)
# regardless of the process working directory (IDE often starts from build/).
target_compile_definitions(test_environment PRIVATE
        "NUMERAIRE_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\""
)

target_link_libraries(test_environment
        PRIVATE
        numeraire_core
        numeraire_database
        numeraire_schedule
        numeraire_utils
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(test_environment)
