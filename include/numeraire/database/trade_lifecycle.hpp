#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace numeraire::database {

/// Trades whose last leg expiry is strictly before `as_of_iso` (day after maturity).
struct TradeLifecycleResult {
    std::vector<std::string> expired_trade_ids;
};

/// Promote matured `LIVE` trades to `EXPIRED` before MTM / simulation / calibration.
///
/// Rule: a trade stays `LIVE` through its expiry calendar date; from the first session
/// with `as_of` **after** `max(leg expiry_date)` it becomes `EXPIRED`.
/// When `portfolio_id` is set, only trades in that portfolio are considered.
[[nodiscard]] TradeLifecycleResult ApplyTradeLifecycleAsOf(
        const std::string& database_file_path,
        std::string_view as_of_iso,
        const std::optional<std::string_view> portfolio_id = std::nullopt);

}  // namespace numeraire::database
