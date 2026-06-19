#pragma once

#include <numeraire/database/historical_calibration_types.hpp>

#include <string>
#include <vector>

namespace numeraire::database {

/// Persists one historical GBM calibration snapshot (header + factor / correlation / Cholesky rows).
class SqliteHistoricalCalibrationRepository {
   public:
    explicit SqliteHistoricalCalibrationRepository(std::string database_file_path);

    /// Replaces any existing row for `(calibration_scope, scope_key, as_of, calibration_method)`.
    /// Returns the new `calibration_id`.
    [[nodiscard]] long UpsertSnapshot(const HistoricalCalibrationHeaderWrite& header,
                                      const std::vector<HistoricalCalibrationFactorWrite>& factors,
                                      const std::vector<HistoricalCalibrationCorrelationWrite>& correlations,
                                      const std::vector<HistoricalCalibrationCholeskyWrite>& cholesky);

   private:
    std::string database_file_path_;
};

}  // namespace numeraire::database
