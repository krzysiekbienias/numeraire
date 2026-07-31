#pragma once

#include <optional>
#include <string>

namespace numeraire::database {

/// One row for [`trade_leg_exposure_eod`](../../../../sql/schema_v1.sql) before INSERT/UPSERT.
struct TradeLegExposureEodRow {
    std::string as_of;
    std::string trade_id;
    std::string leg_id;
    std::string pillar_id;
    int grid_step{0};
    double year_fraction{};
    std::string exposure_date;
    double ee{};
    double pfe_95{};
    double pfe_97{};
    int num_paths{0};
    int mc_seed{0};
    std::optional<int> calibration_id;
    std::string scope_key;
    std::optional<std::string> batch_run_id;
    std::string pricing_engine;
    std::string calculated_at;
    std::string remarks;
};

}  // namespace numeraire::database
