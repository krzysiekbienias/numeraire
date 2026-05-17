#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace numeraire::database {

/// Read a single **`equity_daily_eod`** `close` (Polygon daily bar).
/// Matches `timespan='1d'`. Compared under `UPPER(ticker)` vs `UPPER(\p ticker)` so ingest symbols need not match DB casing exactly.
///
/// Returns `std::nullopt` when **no row** exists; throws `PersistenceError` on SQLite errors.
[[nodiscard]] std::optional<double> LookupEquityDailyClose(const std::string& database_file_path,
                                                        std::string_view ticker,
                                                        std::string_view as_of_iso_yyyy_mm_dd,
                                                        int adjusted = 1);

}  // namespace numeraire::database
