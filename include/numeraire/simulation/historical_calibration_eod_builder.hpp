#pragma once

#include <numeraire/utils/config.hpp>

#include <cstddef>
#include <string>

namespace numeraire::simulation {

struct HistoricalCalibrationBuildParams {
    std::string database_file_path;
    /// `ALL` for every LIVE portfolio; otherwise `trades.portfolio_id`.
    std::string scope_key{"ALL"};
    std::string as_of;
    std::string batch_run_id;
    int lookback_calendar_days{504};
    std::size_t min_return_observations{60};
    int vol_annualization_days{252};
    int adjusted{1};
};

struct HistoricalCalibrationBuildStats {
    long calibration_id{0};
    int num_factors{0};
    std::size_t num_return_observations{0};
};

/// Calibrate from SQLite EOD history and persist `historical_calibration_*` tables.
[[nodiscard]] HistoricalCalibrationBuildStats BuildHistoricalCalibrationEod(
        const HistoricalCalibrationBuildParams& params);

void PrintHistoricalCalibrationEodBuildUsageLines();

/// Returns exit code if `--calibrate-historical-gbm` was requested; `-1` otherwise.
[[nodiscard]] int TryRunHistoricalCalibrationEodBuild(int argc, char** argv, const numeraire::utils::Config& cfg);

}  // namespace numeraire::simulation
