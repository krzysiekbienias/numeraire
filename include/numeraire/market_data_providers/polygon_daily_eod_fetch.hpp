#pragma once

namespace numeraire::utils {
class Config;
}  // namespace numeraire::utils

namespace numeraire::market_data_providers {

/// Parses `argv` for `--fetch-eod-daily` mode. When matched, fetches Polygon
/// `v2/aggs` daily bars for each `--ticker` in `[--from,--to]` and upserts into
/// `equity_daily_eod`, then returns \p `exit_code` (0 or 1).
///
/// \return `-1` if this is not fetch mode (caller should run pricing flow).
/// \return `0` or `1` if fetch mode ran (success or failure).
[[nodiscard]] int TryRunPolygonDailyEodFetch(int argc, char** argv, const numeraire::utils::Config& cfg);

void PrintFetchUsageLines();

}  // namespace numeraire::market_data_providers
