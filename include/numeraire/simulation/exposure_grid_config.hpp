#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace numeraire::simulation {

/// One pillar in the committed exposure **pattern** (`target_dte_days` offset from
/// valuation date; not an absolute calendar date).
struct ExposurePillarConfig {
    std::string id;
    int target_dte_days{0};
    std::string tier;
};

struct ExposureGridConventions {
    bool include_valuation_date{true};
    bool clip_to_book_max_expiry{true};
    int min_horizon_days{365};
};

/// JSON template for exposure simulation / CCR profile grids.
struct ExposureGridConfig {
    std::string schema_version;
    std::string name;
    ExposureGridConventions conventions;
    std::vector<ExposurePillarConfig> exposure_pillars;
};

/// Load [`configs/simulation_exposure_grid.json`](../../configs/simulation_exposure_grid.json) layout.
[[nodiscard]] ExposureGridConfig LoadExposureGridConfig(const std::filesystem::path& path);

/// Resolve path from `simulation.exposure_grid_config` in `configs/default.json` when present.
[[nodiscard]] std::optional<std::filesystem::path> ExposureGridConfigPathFromDefaults(
        const std::filesystem::path& default_json_path);

}  // namespace numeraire::simulation
