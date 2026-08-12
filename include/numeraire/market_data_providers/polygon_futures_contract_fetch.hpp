#pragma once

namespace numeraire::utils {
class Config;
}  // namespace numeraire::utils

namespace numeraire::market_data_providers {

/// Massive/Polygon `GET /futures/v1/contracts` → `futures_contract` for universe commodities.
///
/// \return `-1` if not `--fetch-futures-contracts` mode.
[[nodiscard]] int TryRunPolygonFuturesContractFetch(int argc, char** argv, const numeraire::utils::Config& cfg);

void PrintFuturesContractFetchUsageLines();

}  // namespace numeraire::market_data_providers
