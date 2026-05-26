add_library(numeraire_products STATIC
        equity_asset_or_nothing_product.cpp
        equity_cash_or_nothing_product.cpp
        equity_forward_product.cpp
        product_factory.cpp
        vanilla_equity_option_product.cpp
)

target_include_directories(numeraire_products
        PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(numeraire_products
        PUBLIC
        numeraire_core
        numeraire_database
        numeraire_schedule
        numeraire_utils
)
