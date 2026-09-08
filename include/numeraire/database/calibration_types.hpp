#pragma once

#include <optional>
#include <string>
#include <vector>

namespace numeraire::database {

/// Estimation source of a calibration snapshot (`calibration_snapshot.source`).
namespace calibration_source {
inline constexpr const char* kHistorical = "historical";
inline constexpr const char* kImplied = "implied";
inline constexpr const char* kFit = "fit";
}  // namespace calibration_source

/// Stochastic model a calibration snapshot parameterises (`calibration_snapshot.model`).
namespace calibration_model {
inline constexpr const char* kGbm = "gbm";
inline constexpr const char* kGabillon2F = "gabillon_2f";
}  // namespace calibration_model

/// Bucketing of a calibration snapshot (`calibration_snapshot.calibration_scope`).
namespace calibration_scope {
inline constexpr const char* kBook = "book";
inline constexpr const char* kUnderlying = "underlying";
}  // namespace calibration_scope

/// Header row of `calibration_snapshot`.
///
/// `model` and `source` are independent: the same book and `as_of` can hold a
/// historical GBM snapshot next to an implied Gabillon one. The `history_*` /
/// `lookback_*` / `eod_*` fields describe EOD sampling and stay unset unless
/// `source == calibration_source::kHistorical`.
struct CalibrationHeaderWrite {
    std::string model{calibration_model::kGbm};
    std::string source{calibration_source::kHistorical};
    std::string calibration_scope{database::calibration_scope::kBook};
    std::string scope_key;
    std::string as_of;
    int num_factors{0};
    std::optional<std::string> history_start;
    std::optional<std::string> history_end;
    std::optional<int> lookback_calendar_days;
    std::optional<int> min_return_observations;
    std::optional<int> vol_annualization_days;
    std::optional<int> eod_adjusted;
    std::optional<int> num_return_observations;
    std::string batch_run_id;
    std::string produced_by{"dev_main"};
    std::string remarks;
};

/// One diffusion factor: an equity underlying, a commodity pillar (`CL_M1`) or a
/// model state variable (`CL_SHORT`). `factor_level` is unset for factors with no
/// directly observable level.
struct CalibrationFactorWrite {
    int factor_index{0};
    std::string factor_id;
    std::optional<double> factor_level;
    double volatility{0.0};
};

struct CalibrationCorrelationWrite {
    int factor_i{0};
    int factor_j{0};
    double rho{0.0};
};

struct CalibrationCholeskyWrite {
    int row_i{0};
    int col_j{0};
    double l_value{0.0};
};

/// Model-specific scalar (Gabillon `kappa`, Heston `theta`, ...).
/// Empty `factor_id` scopes the parameter to the whole snapshot.
struct CalibrationParamWrite {
    std::string factor_id;
    std::string param_name;
    double param_value{0.0};
};

}  // namespace numeraire::database
