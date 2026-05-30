add_library(numeraire_database STATIC
        equity_daily_eod_lookup.cpp
        index_daily_eod_lookup.cpp
        leg_mtm_pnl.cpp
        leg_pv.cpp
        trade_booking_rules.cpp
        sqlite_schema.cpp
        sqlite_trade_leg_booking_repository.cpp
        sqlite_trade_leg_mtm_repository.cpp
        sqlite_trade_repository.cpp
        sqlite_vol_surface_repository.cpp
        sqlite_vol_surface_eod_read.cpp
        option_universe_grid_config.cpp
        option_universe_eod_builder.cpp
        discount_curve_eod_builder.cpp
        par_curve_quote_loader.cpp
        sqlite_discount_curve_repository.cpp
        sqlite_discount_curve_eod_read.cpp
        vol_surface_eod_builder.cpp
        vol_surface_quote_loader.cpp
)

target_include_directories(numeraire_database
        PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(numeraire_database
        PUBLIC
        numeraire_enums
        numeraire_quant
        numeraire_schedule
        numeraire_utils
        PRIVATE
        SQLiteCpp
        nlohmann_json::nlohmann_json
)
