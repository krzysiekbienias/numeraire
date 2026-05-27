#pragma once

namespace numeraire::utils {
class Config;
}  // namespace numeraire::utils

namespace numeraire::market_data_providers {

/// Polygon `v2/aggs` daily bars per `option_ticker` → `option_daily_price_eod`.
///
/// Tickers from repeated `--option-ticker`, or from `option_contract` when
/// `--listing-as-of` + `--underlying` are set.
///
/// \return `-1` if not `--fetch-option-daily-price-eod` mode.
[[nodiscard]] int TryRunPolygonOptionDailyPriceEodFetch(int argc, char** argv, const numeraire::utils::Config& cfg);

void PrintOptionDailyPriceEodFetchUsageLines();

}  // namespace numeraire::market_data_providers
