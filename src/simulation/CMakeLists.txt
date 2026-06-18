# Simulation — SoA Monte Carlo risk-factor engine + path-wise pricing.
# Walking skeleton: one compiled TU so the library and its tests link end-to-end.
# Dependencies (quant, schedule, market_data, ...) are added as each step ships.
add_library(numeraire_simulation STATIC
        simulation_module.cpp
        scenario_buffer.cpp
)

target_include_directories(numeraire_simulation
        PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)
