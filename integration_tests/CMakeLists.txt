# Integration tests for Numeraire++
#
# Placeholder for Sprint 0. Real integration tests arrive in Stage 2+ when
# I/O modules ship:
#   - SQLite trade repository (Stage 2)
#   - Polygon.io market data provider (Stage 3+)
#   - Cached market environment round-trips
#
# When sources are added under integration_tests/ they will be picked up
# automatically (CONFIGURE_DEPENDS).

file(GLOB_RECURSE INTEGRATION_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/*/test_*.cpp"
)

if(INTEGRATION_TEST_SOURCES STREQUAL "")
    message(STATUS "integration_tests: no sources yet (placeholder for Stage 2+).")
    return()
endif()

add_executable(integration_test_environment ${INTEGRATION_TEST_SOURCES})
set_target_properties(integration_test_environment PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

target_link_libraries(integration_test_environment
        PRIVATE
        numeraire_utils
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(integration_test_environment)
