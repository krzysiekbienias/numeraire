#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace numeraire::database {

/// One **`equity_daily_eod`** / **`index_daily_eod`** session close (sorted ascending by `as_of`).
struct DailyCloseObservation {
    std::string as_of;
    double close{0.0};
};

/// Load daily closes for a book **`underlying_id`** between inclusive ISO dates.
/// Tries `equity_daily_eod` first, then index mapping (e.g. NDX → I:NDX in `index_daily_eod`).
[[nodiscard]] std::vector<DailyCloseObservation> LoadUnderlyingDailyClosesRange(
        const std::string& database_file_path,
        std::string_view underlying_id,
        std::string_view from_iso_yyyy_mm_dd,
        std::string_view to_iso_yyyy_mm_dd,
        int adjusted = 1);

/// Single-day close for universe / vol-surface spot: equity first, then NDX→I:NDX index map.
[[nodiscard]] std::optional<double> LookupUnderlyingDailyClose(const std::string& database_file_path,
                                                               std::string_view underlying_id,
                                                               std::string_view as_of_iso_yyyy_mm_dd,
                                                               int adjusted = 1);

/// Distinct `products.underlying_id` from booked legs. Default filter: `trades.status = 'LIVE'`.
/// When `portfolio_id` is set, only legs from that `trades.portfolio_id` are included.
[[nodiscard]] std::vector<std::string> ListDistinctBookUnderlyingIds(
        const std::string& database_file_path,
        std::optional<std::string_view> trade_status = std::string_view{"LIVE"},
        std::optional<std::string_view> portfolio_id = std::nullopt);

/// A booked underlying together with the asset class it belongs to.
struct BookUnderlying {
    std::string underlying_id;
    /// `products.asset_kind`, e.g. 'EQUITY' or 'COMMODITY'. Decides how history is
    /// sourced: one close series per equity, a pillar strip per commodity curve.
    std::string asset_kind;
};

/// Same selection as `ListDistinctBookUnderlyingIds`, carrying `products.asset_kind`.
[[nodiscard]] std::vector<BookUnderlying> ListBookUnderlyings(
        const std::string& database_file_path,
        std::optional<std::string_view> trade_status = std::string_view{"LIVE"},
        std::optional<std::string_view> portfolio_id = std::nullopt);

/// A dated futures contract referenced by a booked leg.
struct BookFuturesContract {
    std::string contract_ticker;
    std::string product_code;
    std::string settlement_date;
};

/// Distinct futures contracts booked in `portfolio_id`, ordered by ticker.
///
/// Lighter than loading priceable legs, which is what lets a simulation decide which
/// contracts to carry as risk factors before the factor set those legs will bind to
/// exists.
[[nodiscard]] std::vector<BookFuturesContract> ListBookFuturesContracts(
        const std::string& database_file_path,
        std::string_view portfolio_id,
        std::optional<std::string_view> trade_status = std::string_view{"LIVE"});

}  // namespace numeraire::database
