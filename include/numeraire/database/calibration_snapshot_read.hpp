#pragma once

#include <numeraire/database/calibration_types.hpp>
#include <numeraire/quant/cholesky.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace numeraire::database {

/// One `calibration_param` row.
struct CalibrationParamRead {
    std::string factor_id;
    std::string param_name;
    double param_value{0.0};
};

/// A calibration snapshot loaded from `calibration_snapshot` + its child tables.
///
/// The `history_*` / `lookback_*` / `eod_*` fields are only populated for
/// `source == "historical"`; every other source leaves them unset.
struct CalibrationSnapshotRead {
    long calibration_id{0};
    std::string model;
    std::string source;
    std::string calibration_scope;
    std::string scope_key;
    std::string as_of;
    int num_factors{0};
    std::optional<std::string> history_start;
    std::optional<std::string> history_end;
    std::optional<int> lookback_calendar_days;
    std::optional<int> min_return_observations;
    std::optional<int> vol_annualization_days;
    std::optional<int> eod_adjusted;
    std::optional<std::size_t> num_return_observations;
    std::string batch_run_id;

    std::vector<std::string> factor_ids;
    /// Level per factor, unset for factors with no directly observable level.
    std::vector<std::optional<double>> factor_levels;
    std::vector<double> volatilities;
    /// Symmetric correlation matrix, row-major (`n * n`), unit diagonal.
    std::vector<double> correlation;
    quant::CholeskyFactor cholesky;
    /// Model-specific scalars; empty for models fully described by the factor rows.
    std::vector<CalibrationParamRead> params;

    /// Looks up a `calibration_param` value; empty `factor_id` targets snapshot-level params.
    [[nodiscard]] std::optional<double> TryGetParam(std::string_view param_name,
                                                    std::string_view factor_id = "") const;
};

/// True when a snapshot exists for the exact `(scope, scope_key, as_of, model, source)` key.
[[nodiscard]] bool HasCalibrationSnapshot(const std::string& database_file_path,
                                          std::string_view scope_key,
                                          std::string_view as_of_iso_yyyy_mm_dd,
                                          std::string_view model = calibration_model::kGbm,
                                          std::string_view source = calibration_source::kHistorical,
                                          std::string_view scope = calibration_scope::kBook);

/// Loads the latest snapshot with `as_of <= requested_as_of` for `(scope, scope_key, model, source)`.
[[nodiscard]] std::optional<CalibrationSnapshotRead> TryLoadLatestCalibrationSnapshot(
        const std::string& database_file_path,
        std::string_view scope_key,
        std::string_view on_or_before_as_of_iso_yyyy_mm_dd,
        std::string_view model = calibration_model::kGbm,
        std::string_view source = calibration_source::kHistorical,
        std::string_view scope = calibration_scope::kBook);

}  // namespace numeraire::database
