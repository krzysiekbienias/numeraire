#pragma once

#include <numeraire/quant/cholesky.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace numeraire::database {

/// Historical GBM calibration snapshot loaded from `historical_calibration_*`.
struct HistoricalCalibrationEodRead {
    long calibration_id{0};
    std::string calibration_method;
    std::string calibration_scope;
    std::string scope_key;
    std::string as_of;
    std::string history_start;
    std::string history_end;
    int lookback_calendar_days{0};
    int min_return_observations{0};
    int vol_annualization_days{252};
    int eod_adjusted{1};
    int num_factors{0};
    std::size_t num_return_observations{0};
    std::string batch_run_id;

    std::vector<std::string> factor_ids;
    std::vector<double> spots_as_of;
    std::vector<double> volatilities;
    /// Symmetric correlation matrix, row-major (`n * n`), unit diagonal.
    std::vector<double> correlation;
    quant::CholeskyFactor cholesky;
};

/// True when an official header exists for the exact `(scope_key, as_of, method)` key.
[[nodiscard]] bool HasHistoricalCalibrationEod(const std::string& database_file_path,
                                               std::string_view scope_key,
                                               std::string_view as_of_iso_yyyy_mm_dd,
                                               std::string_view calibration_method = "historical_eod_gbm");

/// Loads the latest snapshot with `as_of <= requested_as_of` for `(scope, scope_key, method)`.
[[nodiscard]] std::optional<HistoricalCalibrationEodRead> TryLoadLatestHistoricalCalibrationEod(
        const std::string& database_file_path,
        std::string_view scope_key,
        std::string_view on_or_before_as_of_iso_yyyy_mm_dd,
        std::string_view calibration_method = "historical_eod_gbm",
        std::string_view calibration_scope = "book");

}  // namespace numeraire::database
