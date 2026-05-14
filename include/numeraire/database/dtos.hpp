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
    std::string expiry_date;
    std::optional<SettlementType> settlement;
    std::optional<DayCount> day_count;
    std::optional<CalendarType> calendar;
};

/// Booked **trade** referencing `product_id`; timestamps stay as strings matching
/// SQLite `TEXT` until the stack adopts a shared instant/date type.
struct TradeDto {
    std::string trade_id;
    std::string product_id;
    std::string booking_timestamp;
    std::string trade_date;
    std::string updated_at;
    std::string status;
    PositionDirection direction{};
    double quantity{};
    std::optional<double> commission;
};

}  // namespace numeraire::database
