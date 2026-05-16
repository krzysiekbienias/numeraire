#pragma once

namespace numeraire::utils {
class Config;
}  // namespace numeraire::utils

namespace numeraire::market_data_providers {

/// Polygon `v2/aggs` daily bars for index tickers (e.g. `I:SPX`) into `index_daily_eod`
/// (same REST API whether `POLYGON_BASE_URL` is api.polygon.io or rebranded api.massive.com).
///
/// \return `-1` if not `--fetch-index-eod-daily` mode.
[[nodiscard]] int TryRunPolygonIndexDailyEodFetch(int argc, char** argv, const numeraire::utils::Config& cfg);

void PrintIndexFetchUsageLines();

}  // namespace numeraire::market_data_providers
