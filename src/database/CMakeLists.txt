add_library(numeraire_database STATIC
        equity_daily_eod_lookup.cpp
        leg_mtm_pnl.cpp
        leg_pv.cpp
        trade_booking_rules.cpp
        sqlite_schema.cpp
        sqlite_trade_leg_booking_repository.cpp
        sqlite_trade_leg_mtm_repository.cpp
        sqlite_trade_repository.cpp
)

target_include_directories(numeraire_database
        PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(numeraire_database
        PUBLIC
        numeraire_enums
        numeraire_utils
        PRIVATE
        SQLiteCpp
)
