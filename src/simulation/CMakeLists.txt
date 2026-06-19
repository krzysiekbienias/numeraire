# Simulation — SoA Monte Carlo risk-factor engine + path-wise pricing.
# Walking skeleton: one compiled TU so the library and its tests link end-to-end.
# Dependencies (quant, schedule, market_data, ...) are added as each step ships.
add_library(numeraire_simulation STATIC
        simulation_module.cpp
        scenario_buffer.cpp
        random_engine.cpp
        normal_random.cpp
        exposure_grid_config.cpp
        exposure_time_grid.cpp
        gbm_evolution.cpp
        scenario_dump.cpp
        historical_calibrator.cpp
)

target_include_directories(numeraire_simulation
        PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(numeraire_simulation
        PUBLIC
        numeraire_schedule
        numeraire_quant
        PRIVATE
        numeraire_utils
        numeraire_database
)
