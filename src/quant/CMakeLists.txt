add_library(numeraire_quant STATIC
        black_scholes_vanilla.cpp
        cholesky.cpp
        discount_curve_bootstrap.cpp
        fixed_income_pricing.cpp
        implied_vol_european.cpp
        nearest_correlation.cpp
)

target_include_directories(numeraire_quant
        PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(numeraire_quant
        PUBLIC
        numeraire_enums
)
