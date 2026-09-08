#include <numeraire/database/calibration_snapshot_read.hpp>

#include <numeraire/utils/exception.hpp>

#include <SQLiteCpp/SQLiteCpp.h>

#include <string>
#include <vector>

namespace numeraire::database {
namespace {

void ReconstructCorrelationMatrix(const std::size_t n,
                                  const std::vector<std::pair<int, int>>& indices,
                                  const std::vector<double>& rhos,
                                  std::vector<double>& out) {
    out.assign(n * n, 0.0);
    for (std::size_t k = 0; k < indices.size(); ++k) {
        const std::size_t i = static_cast<std::size_t>(indices[k].first);
        const std::size_t j = static_cast<std::size_t>(indices[k].second);
        out[(i * n) + j] = rhos[k];
        out[(j * n) + i] = rhos[k];
    }
}

void ReconstructCholeskyFactor(const std::size_t n,
                               const std::vector<std::pair<int, int>>& indices,
                               const std::vector<double>& values,
                               quant::CholeskyFactor& out) {
    out.n = n;
    out.lower.assign(n * n, 0.0);
    for (std::size_t k = 0; k < indices.size(); ++k) {
        const std::size_t row = static_cast<std::size_t>(indices[k].first);
        const std::size_t col = static_cast<std::size_t>(indices[k].second);
        out.lower[(row * n) + col] = values[k];
    }
}

[[nodiscard]] std::optional<std::string> OptionalText(const SQLite::Column& column) {
    if (column.isNull()) {
        return std::nullopt;
    }
    return column.getString();
}

[[nodiscard]] std::optional<int> OptionalInt(const SQLite::Column& column) {
    if (column.isNull()) {
        return std::nullopt;
    }
    return column.getInt();
}

}  // namespace

std::optional<double> CalibrationSnapshotRead::TryGetParam(const std::string_view param_name,
                                                           const std::string_view factor_id) const {
    for (const CalibrationParamRead& param : params) {
        if (param.param_name == param_name && param.factor_id == factor_id) {
            return param.param_value;
        }
    }
    return std::nullopt;
}

bool HasCalibrationSnapshot(const std::string& database_file_path,
                            const std::string_view scope_key,
                            const std::string_view as_of_iso_yyyy_mm_dd,
                            const std::string_view model,
                            const std::string_view source,
                            const std::string_view scope) {
    try {
        SQLite::Database db(database_file_path, SQLite::OPEN_READONLY);
        SQLite::Statement st(db,
                             "SELECT 1 FROM calibration_snapshot "
                             "WHERE calibration_scope = ? AND scope_key = ? AND as_of = ? "
                             "AND model = ? AND source = ? LIMIT 1");
        st.bind(1, std::string(scope));
        st.bind(2, std::string(scope_key));
        st.bind(3, std::string(as_of_iso_yyyy_mm_dd));
        st.bind(4, std::string(model));
        st.bind(5, std::string(source));
        return st.executeStep();
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"HasCalibrationSnapshot: "} + e.what());
    }
}

std::optional<CalibrationSnapshotRead> TryLoadLatestCalibrationSnapshot(
        const std::string& database_file_path,
        const std::string_view scope_key,
        const std::string_view on_or_before_as_of_iso_yyyy_mm_dd,
        const std::string_view model,
        const std::string_view source,
        const std::string_view scope) {
    try {
        SQLite::Database db(database_file_path, SQLite::OPEN_READONLY);
        SQLite::Statement hdr(
                db,
                "SELECT calibration_id, model, source, calibration_scope, scope_key, as_of, num_factors, "
                "history_start, history_end, lookback_calendar_days, min_return_observations, "
                "vol_annualization_days, eod_adjusted, num_return_observations, batch_run_id "
                "FROM calibration_snapshot "
                "WHERE calibration_scope = ? AND scope_key = ? AND model = ? AND source = ? "
                "AND as_of <= ? "
                "ORDER BY as_of DESC LIMIT 1");
        hdr.bind(1, std::string(scope));
        hdr.bind(2, std::string(scope_key));
        hdr.bind(3, std::string(model));
        hdr.bind(4, std::string(source));
        hdr.bind(5, std::string(on_or_before_as_of_iso_yyyy_mm_dd));
        if (!hdr.executeStep()) {
            return std::nullopt;
        }

        CalibrationSnapshotRead out{};
        out.calibration_id = hdr.getColumn(0).getInt64();
        out.model = hdr.getColumn(1).getString();
        out.source = hdr.getColumn(2).getString();
        out.calibration_scope = hdr.getColumn(3).getString();
        out.scope_key = hdr.getColumn(4).getString();
        out.as_of = hdr.getColumn(5).getString();
        out.num_factors = hdr.getColumn(6).getInt();
        out.history_start = OptionalText(hdr.getColumn(7));
        out.history_end = OptionalText(hdr.getColumn(8));
        out.lookback_calendar_days = OptionalInt(hdr.getColumn(9));
        out.min_return_observations = OptionalInt(hdr.getColumn(10));
        out.vol_annualization_days = OptionalInt(hdr.getColumn(11));
        out.eod_adjusted = OptionalInt(hdr.getColumn(12));
        if (!hdr.getColumn(13).isNull()) {
            out.num_return_observations = static_cast<std::size_t>(hdr.getColumn(13).getInt64());
        }
        if (!hdr.getColumn(14).isNull()) {
            out.batch_run_id = hdr.getColumn(14).getString();
        }

        const long calibration_id = out.calibration_id;
        const std::size_t n = static_cast<std::size_t>(out.num_factors);
        if (n == 0) {
            return std::nullopt;
        }

        SQLite::Statement factors(db,
                                  "SELECT factor_index, factor_id, factor_level, volatility "
                                  "FROM calibration_factor "
                                  "WHERE calibration_id = ? ORDER BY factor_index ASC");
        factors.bind(1, calibration_id);
        out.factor_ids.reserve(n);
        out.factor_levels.reserve(n);
        out.volatilities.reserve(n);
        while (factors.executeStep()) {
            out.factor_ids.emplace_back(factors.getColumn(1).getString());
            out.factor_levels.push_back(factors.getColumn(2).isNull()
                                                ? std::nullopt
                                                : std::optional<double>{factors.getColumn(2).getDouble()});
            out.volatilities.push_back(factors.getColumn(3).getDouble());
        }
        if (out.factor_ids.size() != n) {
            throw PersistenceError("TryLoadLatestCalibrationSnapshot: factor row count mismatch.");
        }

        std::vector<std::pair<int, int>> corr_indices;
        std::vector<double> corr_values;
        SQLite::Statement corr(db,
                               "SELECT factor_i, factor_j, rho FROM calibration_correlation "
                               "WHERE calibration_id = ? ORDER BY factor_i ASC, factor_j ASC");
        corr.bind(1, calibration_id);
        while (corr.executeStep()) {
            corr_indices.emplace_back(corr.getColumn(0).getInt(), corr.getColumn(1).getInt());
            corr_values.push_back(corr.getColumn(2).getDouble());
        }
        ReconstructCorrelationMatrix(n, corr_indices, corr_values, out.correlation);

        std::vector<std::pair<int, int>> chol_indices;
        std::vector<double> chol_values;
        SQLite::Statement chol(db,
                               "SELECT row_i, col_j, l_value FROM calibration_cholesky "
                               "WHERE calibration_id = ? ORDER BY row_i ASC, col_j ASC");
        chol.bind(1, calibration_id);
        while (chol.executeStep()) {
            chol_indices.emplace_back(chol.getColumn(0).getInt(), chol.getColumn(1).getInt());
            chol_values.push_back(chol.getColumn(2).getDouble());
        }
        ReconstructCholeskyFactor(n, chol_indices, chol_values, out.cholesky);

        if (out.cholesky.n != n || out.correlation.size() != n * n) {
            throw PersistenceError("TryLoadLatestCalibrationSnapshot: reconstructed matrix size mismatch.");
        }

        SQLite::Statement param(db,
                                "SELECT factor_id, param_name, param_value FROM calibration_param "
                                "WHERE calibration_id = ? ORDER BY factor_id ASC, param_name ASC");
        param.bind(1, calibration_id);
        while (param.executeStep()) {
            out.params.push_back(CalibrationParamRead{
                    .factor_id = param.getColumn(0).getString(),
                    .param_name = param.getColumn(1).getString(),
                    .param_value = param.getColumn(2).getDouble(),
            });
        }
        return out;
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"TryLoadLatestCalibrationSnapshot: "} + e.what());
    }
}

}  // namespace numeraire::database
