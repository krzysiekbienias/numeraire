#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace numeraire::database {

struct ExpiryPillarConfig {
    std::string id;
    int target_dte_days{0};
    std::string tier;
};

struct OptionUniverseGridConfig {
    std::string schema_version;
    std::string name;
    std::vector<ExpiryPillarConfig> expiry_pillars;
    std::vector<double> otm_moneyness_percent;
    double skip_point_if_strike_gap_exceeds_ratio{0.75};
};

/// Loads [`configs/option_universe_grid.json`](../configs/option_universe_grid.json) layout.
[[nodiscard]] OptionUniverseGridConfig LoadOptionUniverseGridConfig(const std::filesystem::path& path);

}  // namespace numeraire::database
