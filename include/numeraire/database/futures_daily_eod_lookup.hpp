#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace numeraire::database {

/// Read a single **`futures_daily_eod`** mark for a contract ticker.
/// Prefers `settlement_price` when present and finite; otherwise `close`.
/// Matches `timespan='1session'`. Compared under `UPPER(ticker)` vs `UPPER(\p ticker)`.
///
/// Returns `std::nullopt` when **no row** exists; throws `PersistenceError` on SQLite errors.
[[nodiscard]] std::optional<double> LookupFuturesDailySettlement(
        const std::string& database_file_path, std::string_view ticker,
        std::string_view as_of_iso_yyyy_mm_dd);

}  // namespace numeraire::database
