add_library(numeraire_pricers STATIC
        analytic_black_scholes_equity_pricer.cpp
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
)
