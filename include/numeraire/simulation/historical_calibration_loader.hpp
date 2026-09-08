#pragma once

#include <numeraire/database/calibration_snapshot_read.hpp>
#include <numeraire/simulation/gbm_spec.hpp>
#include <numeraire/simulation/historical_calibrator.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace numeraire::simulation {

/// Convert a persisted snapshot into the in-memory calibrator result shape.
/// Requires a historically sourced snapshot: throws when the history window or a
/// factor level is missing.
[[nodiscard]] HistoricalCalibrationResult ToHistoricalCalibrationResult(
        const database::CalibrationSnapshotRead& read);

/// Latest historical GBM snapshot with `as_of <= on_or_before_as_of` for `scope_key`
/// (e.g. `ALL`, `BOOK_1`).
[[nodiscard]] std::optional<HistoricalCalibrationResult> TryLoadHistoricalCalibrationFromDatabase(
        const std::string& database_file_path,
        std::string_view scope_key,
        std::string_view on_or_before_as_of_iso_yyyy_mm_dd,
        std::string_view model = database::calibration_model::kGbm);

/// Loads calibration from SQLite and builds a `MultiFactorGbmSpec` ready for `EvolveMultiFactorGbm`.
[[nodiscard]] std::optional<MultiFactorGbmSpec> TryLoadMultiFactorGbmSpecFromDatabase(
        const std::string& database_file_path,
        std::string_view scope_key,
        std::string_view on_or_before_as_of_iso_yyyy_mm_dd,
        double risk_free_rate,
        double dividend_yield = 0.0,
        std::string_view model = database::calibration_model::kGbm);

}  // namespace numeraire::simulation
