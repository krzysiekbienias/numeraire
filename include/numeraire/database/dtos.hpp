#pragma once

#include <numeraire/enums/calendar_type.hpp>
#include <numeraire/enums/day_count.hpp>
#include <numeraire/enums/position_direction.hpp>
#include <numeraire/enums/settlement_type.hpp>
#include <optional>
#include <string>

namespace numeraire::database {

/// Snapshot of the standalone **equity-product** facet (`products_equity` JOIN).
/// `option_side` / `strike` may be empty for multi-leg structures.
/// `catalog_instrument_type` / `catalog_exercise_style` mirror DB columns when
/// loaded by `SqliteTradeRepository`; `attributes_json` is `structured_params`
/// payload (extras only once `instrument_type` lives mostly on the catalog row).
struct ProductDto {
    std::string product_id;
    std::optional<std::string> option_side;
    std::optional<double> strike;
    std::optional<std::string> catalog_instrument_type;
    std::optional<std::string> catalog_exercise_style;
    std::string attributes_json;
};

/// Equity-backed row keyed by the same `product_id` (underlying, expiry,
/// settlement, conventions). `asset_kind` mirrors the DB discriminator (e.g.
/// `"EQUITY"`) until a typed hierarchy ships.
struct ProductEquityDto {
    std::string product_id;
    std::string asset_kind;
    std::string underlying_id;
    std::optional<std::string> expiry_date;
    std::string currency;
    double contract_size{100.0};
    std::optional<SettlementType> settlement;
    std::optional<DayCount> day_count;
    std::optional<CalendarType> calendar;
};

/// Trade header (`trades` row). `trade_date` / timestamps may be empty strings
/// when the column is NULL in SQLite.
struct TradeHeaderDto {
    std::string trade_id;
    std::string portfolio_id;
    std::string strategy_type;
    std::string booking_timestamp;
    std::string trade_date;
    std::string updated_at;
    std::string status;
};

/// Single booked leg (`trade_legs`).
struct TradeLegDto {
    std::string leg_id;
    std::string trade_id;
    std::string product_id;
    PositionDirection direction{};
    double quantity{};
    double execution_price{};
    std::optional<double> commission;
};

}  // namespace numeraire::database
