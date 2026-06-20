#include <numeraire/simulation/historical_calibration_loader.hpp>

#include <numeraire/schedule/date.hpp>
#include <numeraire/utils/exception.hpp>

namespace numeraire::simulation {

HistoricalCalibrationResult ToHistoricalCalibrationResult(
        const database::HistoricalCalibrationEodRead& read) {
    if (read.factor_ids.empty()) {
        throw ValidationError("ToHistoricalCalibrationResult: calibration has no factors.");
    }
    const std::size_t n = read.factor_ids.size();
    if (read.spots_as_of.size() != n || read.volatilities.size() != n || read.cholesky.n != n ||
        read.correlation.size() != n * n) {
        throw ValidationError("ToHistoricalCalibrationResult: inconsistent calibration vector sizes.");
    }

    HistoricalCalibrationResult out;
    out.factor_ids = read.factor_ids;
    out.spots_as_of = read.spots_as_of;
    out.volatilities = read.volatilities;
    out.correlation = read.correlation;
    out.cholesky = read.cholesky;
    out.num_return_observations = read.num_return_observations;
    out.history_start = schedule::ParseIsoDate(read.history_start);
    out.history_end = schedule::ParseIsoDate(read.history_end);
    return out;
}

std::optional<HistoricalCalibrationResult> TryLoadHistoricalCalibrationFromDatabase(
        const std::string& database_file_path,
        const std::string_view scope_key,
        const std::string_view on_or_before_as_of_iso_yyyy_mm_dd,
        const std::string_view calibration_method) {
    const std::optional<database::HistoricalCalibrationEodRead> read =
            database::TryLoadLatestHistoricalCalibrationEod(database_file_path,
                                                              scope_key,
                                                              on_or_before_as_of_iso_yyyy_mm_dd,
                                                              calibration_method);
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
        const std::string_view calibration_method) {
    const std::optional<HistoricalCalibrationResult> calibration =
            TryLoadHistoricalCalibrationFromDatabase(database_file_path,
                                                     scope_key,
                                                     on_or_before_as_of_iso_yyyy_mm_dd,
                                                     calibration_method);
    if (!calibration.has_value()) {
        return std::nullopt;
    }
    return BuildMultiFactorGbmSpec(*calibration, risk_free_rate, dividend_yield);
}

}  // namespace numeraire::simulation
