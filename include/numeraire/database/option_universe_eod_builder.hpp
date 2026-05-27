#pragma once

#include <numeraire/database/option_universe_grid_config.hpp>
#include <numeraire/utils/config.hpp>

#include <filesystem>
#include <string>

namespace numeraire::database {

struct OptionUniverseBuildParams {
    std::string database_file_path;
    std::string listing_as_of;
    std::string underlying_ticker;
    std::string index_ticker;
    std::filesystem::path grid_config_path{"configs/option_universe_grid.json"};
    int spot_adjusted{1};
};

struct OptionUniverseBuildStats {
    int points_requested{0};
    int points_matched{0};
    int points_skipped_no_contract{0};
    int points_skipped_strike_gap{0};
    int rows_upserted{0};
};

[[nodiscard]] OptionUniverseBuildStats BuildOptionUniverseEod(const OptionUniverseBuildParams& params,
                                                              const OptionUniverseGridConfig& grid);

void PrintOptionUniverseEodBuildUsageLines();

/// Returns exit code if `--build-option-universe` was requested; `-1` otherwise.
[[nodiscard]] int TryRunOptionUniverseEodBuild(int argc, char** argv, const numeraire::utils::Config& cfg);

}  // namespace numeraire::database
