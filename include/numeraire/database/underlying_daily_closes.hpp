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

/// Distinct `products.underlying_id` from booked legs. Default filter: `trades.status = 'LIVE'`.
/// When `portfolio_id` is set, only legs from that `trades.portfolio_id` are included.
[[nodiscard]] std::vector<std::string> ListDistinctBookUnderlyingIds(
        const std::string& database_file_path,
        std::optional<std::string_view> trade_status = std::string_view{"LIVE"},
        std::optional<std::string_view> portfolio_id = std::nullopt);

}  // namespace numeraire::database
