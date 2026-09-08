#pragma once

#include <numeraire/database/calibration_types.hpp>

#include <string>
#include <vector>

namespace numeraire::database {

/// Persists one calibration snapshot (header + factor / correlation / Cholesky / param rows).
/// Model-agnostic: the caller decides what `model` and `source` the snapshot carries.
class SqliteCalibrationRepository {
   public:
    explicit SqliteCalibrationRepository(std::string database_file_path);

    /// Replaces any existing row for `(calibration_scope, scope_key, as_of, model, source)`.
    /// Returns the new `calibration_id`.
    [[nodiscard]] long UpsertSnapshot(const CalibrationHeaderWrite& header,
                                      const std::vector<CalibrationFactorWrite>& factors,
                                      const std::vector<CalibrationCorrelationWrite>& correlations,
                                      const std::vector<CalibrationCholeskyWrite>& cholesky,
                                      const std::vector<CalibrationParamWrite>& params = {});

   private:
    std::string database_file_path_;
};

}  // namespace numeraire::database
