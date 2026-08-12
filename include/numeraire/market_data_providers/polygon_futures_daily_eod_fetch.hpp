#pragma once

namespace numeraire::utils {
class Config;
}  // namespace numeraire::utils

namespace numeraire::market_data_providers {

/// Massive/Polygon `GET /futures/v1/aggs/{ticker}?resolution=1session` → `futures_daily_eod`.
///
/// \return `-1` if not `--fetch-futures-eod-daily` mode.
[[nodiscard]] int TryRunPolygonFuturesDailyEodFetch(int argc, char** argv, const numeraire::utils::Config& cfg);

void PrintFuturesDailyEodFetchUsageLines();

}  // namespace numeraire::market_data_providers
