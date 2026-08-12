add_library(numeraire_pricers STATIC
        analytic_black_scholes_equity_pricer.cpp
        analytic_composite_pricer.cpp
        analytic_forward_pricer.cpp
        analytic_futures_outright_pricer.cpp
        analytic_spot_pricer.cpp
        binomial_black_scholes_equity_pricer.cpp
        monte_carlo_gbm_european_pricer.cpp
        pricer_factory.cpp
)

target_include_directories(numeraire_pricers
        PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(numeraire_pricers
        PUBLIC
        numeraire_core
        numeraire_products
        numeraire_quant
        numeraire_simulation_core
        numeraire_utils
)
