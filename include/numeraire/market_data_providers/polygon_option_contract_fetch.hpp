#pragma once

namespace numeraire::utils {
class Config;
}  // namespace numeraire::utils

namespace numeraire::market_data_providers {

/// Polygon `v3/reference/options/contracts` → `option_contract`.
/// Default: full European chain per underlying/listing day. Optional `--strike-band` narrows by index close.
///
/// \return `-1` if not `--fetch-option-contracts` mode.
[[nodiscard]] int TryRunPolygonOptionContractFetch(int argc, char** argv, const numeraire::utils::Config& cfg);

void PrintOptionContractFetchUsageLines();

}  // namespace numeraire::market_data_providers
