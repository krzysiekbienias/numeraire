add_library(numeraire_quant STATIC
        black_scholes_vanilla.cpp
        implied_vol_european.cpp
)

target_include_directories(numeraire_quant
        PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(numeraire_quant
        PUBLIC
        numeraire_enums
)
