#pragma once

#include <string>
#include <vector>

namespace numeraire::database {

struct HistoricalCalibrationHeaderWrite {
    std::string calibration_method{"historical_eod_gbm"};
    std::string calibration_scope{"book"};
    std::string scope_key;
    std::string as_of;
    std::string history_start;
    std::string history_end;
    int lookback_calendar_days{0};
    int min_return_observations{0};
    int vol_annualization_days{252};
    int eod_adjusted{1};
    int num_factors{0};
    int num_return_observations{0};
    std::string batch_run_id;
    std::string source{"dev_main"};
    std::string remarks;
};

struct HistoricalCalibrationFactorWrite {
    int factor_index{0};
    std::string underlying_id;
    double spot_as_of{0.0};
    double volatility{0.0};
};

struct HistoricalCalibrationCorrelationWrite {
    int factor_i{0};
    int factor_j{0};
    double rho{0.0};
};

struct HistoricalCalibrationCholeskyWrite {
    int row_i{0};
    int col_j{0};
    double l_value{0.0};
};

}  // namespace numeraire::database
