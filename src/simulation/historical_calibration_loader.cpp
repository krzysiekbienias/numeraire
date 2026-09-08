#include <numeraire/simulation/historical_calibration_loader.hpp>

#include <numeraire/schedule/date.hpp>
#include <numeraire/utils/exception.hpp>

namespace numeraire::simulation {

HistoricalCalibrationResult ToHistoricalCalibrationResult(const database::CalibrationSnapshotRead& read) {
    if (read.factor_ids.empty()) {
        throw ValidationError("ToHistoricalCalibrationResult: calibration has no factors.");
    }
    const std::size_t n = read.factor_ids.size();
    if (read.factor_levels.size() != n || read.volatilities.size() != n || read.cholesky.n != n ||
        read.correlation.size() != n * n) {
        throw ValidationError("ToHistoricalCalibrationResult: inconsistent calibration vector sizes.");
    }
    if (!read.history_start.has_value() || !read.history_end.has_value() ||
        !read.num_return_observations.has_value()) {
        throw ValidationError(
                "ToHistoricalCalibrationResult: snapshot has no history window; expected source='historical'.");
    }

    HistoricalCalibrationResult out;
    out.factor_ids = read.factor_ids;
    out.spots_as_of.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (!read.factor_levels[i].has_value()) {
            throw ValidationError("ToHistoricalCalibrationResult: factor '" + read.factor_ids[i] +
                                  "' has no level; GBM simulation needs one per factor.");
        }
        out.spots_as_of.push_back(*read.factor_levels[i]);
    }
    out.volatilities = read.volatilities;
    out.correlation = read.correlation;
    out.cholesky = read.cholesky;
    out.num_return_observations = *read.num_return_observations;
    out.history_start = schedule::ParseIsoDate(*read.history_start);
    out.history_end = schedule::ParseIsoDate(*read.history_end);
    return out;
}

std::optional<HistoricalCalibrationResult> TryLoadHistoricalCalibrationFromDatabase(
        const std::string& database_file_path,
        const std::string_view scope_key,
        const std::string_view on_or_before_as_of_iso_yyyy_mm_dd,
        const std::string_view model) {
    const std::optional<database::CalibrationSnapshotRead> read =
            database::TryLoadLatestCalibrationSnapshot(database_file_path,
                                                       scope_key,
                                                       on_or_before_as_of_iso_yyyy_mm_dd,
                                                       model,
                                                       database::calibration_source::kHistorical);
    if (!read.has_value()) {
        return std::nullopt;
    }
    return ToHistoricalCalibrationResult(*read);
}

std::optional<MultiFactorGbmSpec> TryLoadMultiFactorGbmSpecFromDatabase(
        const std::string& database_file_path,
        const std::string_view scope_key,
        const std::string_view on_or_before_as_of_iso_yyyy_mm_dd,
        const double risk_free_rate,
        const double dividend_yield,
        const std::string_view model) {
    const std::optional<HistoricalCalibrationResult> calibration =
            TryLoadHistoricalCalibrationFromDatabase(database_file_path,
                                                     scope_key,
                                                     on_or_before_as_of_iso_yyyy_mm_dd,
                                                     model);
    if (!calibration.has_value()) {
        return std::nullopt;
    }
    return BuildMultiFactorGbmSpec(*calibration, risk_free_rate, dividend_yield);
}

}  // namespace numeraire::simulation
