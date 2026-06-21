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
        gbm_spec.cpp
        scenario_dump.cpp
        scenario_slice_market_data.cpp
        path_pricing_market_config.cpp
        exposure_metrics.cpp
        leg_exposure_dump.cpp
        leg_path_pv_buffer.cpp
        path_pricer.cpp
        path_pricing_rules.cpp
        historical_calibrator.cpp
        historical_calibration_eod_builder.cpp
        historical_calibration_loader.cpp
        historical_gbm_simulate.cpp
)

target_include_directories(numeraire_simulation
        PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(numeraire_simulation
        PUBLIC
        numeraire_schedule
        numeraire_quant
        numeraire_core
        PRIVATE
        numeraire_utils
        numeraire_database
        numeraire_market_data
        numeraire_products
        numeraire_pricers
)
