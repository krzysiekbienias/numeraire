#include <numeraire/database/historical_calibration_eod_read.hpp>

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

}  // namespace

bool HasHistoricalCalibrationEod(const std::string& database_file_path,
                                 const std::string_view scope_key,
                                 const std::string_view as_of_iso_yyyy_mm_dd,
                                 const std::string_view calibration_method) {
    try {
        SQLite::Database db(database_file_path, SQLite::OPEN_READONLY);
        SQLite::Statement st(
                db,
                "SELECT 1 FROM historical_calibration "
                "WHERE calibration_scope = 'book' AND scope_key = ? AND as_of = ? "
                "AND calibration_method = ? LIMIT 1");
        st.bind(1, std::string(scope_key));
        st.bind(2, std::string(as_of_iso_yyyy_mm_dd));
        st.bind(3, std::string(calibration_method));
        return st.executeStep();
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"HasHistoricalCalibrationEod: "} + e.what());
    }
}

std::optional<HistoricalCalibrationEodRead> TryLoadLatestHistoricalCalibrationEod(
        const std::string& database_file_path,
        const std::string_view scope_key,
        const std::string_view on_or_before_as_of_iso_yyyy_mm_dd,
        const std::string_view calibration_method,
        const std::string_view calibration_scope) {
    try {
        SQLite::Database db(database_file_path, SQLite::OPEN_READONLY);
        SQLite::Statement hdr(
                db,
                "SELECT calibration_id, calibration_method, calibration_scope, scope_key, as_of, "
                "history_start, history_end, lookback_calendar_days, min_return_observations, "
                "vol_annualization_days, eod_adjusted, num_factors, num_return_observations, batch_run_id "
                "FROM historical_calibration "
                "WHERE calibration_scope = ? AND scope_key = ? AND calibration_method = ? "
                "AND as_of <= ? "
                "ORDER BY as_of DESC LIMIT 1");
        hdr.bind(1, std::string(calibration_scope));
        hdr.bind(2, std::string(scope_key));
        hdr.bind(3, std::string(calibration_method));
        hdr.bind(4, std::string(on_or_before_as_of_iso_yyyy_mm_dd));
        if (!hdr.executeStep()) {
            return std::nullopt;
        }

        HistoricalCalibrationEodRead out{};
        out.calibration_id = hdr.getColumn(0).getInt64();
        out.calibration_method = hdr.getColumn(1).getString();
        out.calibration_scope = hdr.getColumn(2).getString();
        out.scope_key = hdr.getColumn(3).getString();
        out.as_of = hdr.getColumn(4).getString();
        out.history_start = hdr.getColumn(5).getString();
        out.history_end = hdr.getColumn(6).getString();
        out.lookback_calendar_days = hdr.getColumn(7).getInt();
        out.min_return_observations = hdr.getColumn(8).getInt();
        out.vol_annualization_days = hdr.getColumn(9).getInt();
        out.eod_adjusted = hdr.getColumn(10).getInt();
        out.num_factors = hdr.getColumn(11).getInt();
        out.num_return_observations = static_cast<std::size_t>(hdr.getColumn(12).getInt64());
        if (!hdr.getColumn(13).isNull()) {
            out.batch_run_id = hdr.getColumn(13).getString();
        }

        const long calibration_id = out.calibration_id;
        const std::size_t n = static_cast<std::size_t>(out.num_factors);
        if (n == 0) {
            return std::nullopt;
        }

        SQLite::Statement factors(
                db,
                "SELECT factor_index, underlying_id, spot_as_of, volatility "
                "FROM historical_calibration_factor "
                "WHERE calibration_id = ? ORDER BY factor_index ASC");
        factors.bind(1, calibration_id);
        out.factor_ids.reserve(n);
        out.spots_as_of.reserve(n);
        out.volatilities.reserve(n);
        while (factors.executeStep()) {
            out.factor_ids.emplace_back(factors.getColumn(1).getString());
            out.spots_as_of.push_back(factors.getColumn(2).getDouble());
            out.volatilities.push_back(factors.getColumn(3).getDouble());
        }
        if (out.factor_ids.size() != n) {
            throw PersistenceError("TryLoadLatestHistoricalCalibrationEod: factor row count mismatch.");
        }

        std::vector<std::pair<int, int>> corr_indices;
        std::vector<double> corr_values;
        SQLite::Statement corr(db,
                               "SELECT factor_i, factor_j, rho FROM historical_calibration_correlation "
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
                               "SELECT row_i, col_j, l_value FROM historical_calibration_cholesky "
                               "WHERE calibration_id = ? ORDER BY row_i ASC, col_j ASC");
        chol.bind(1, calibration_id);
        while (chol.executeStep()) {
            chol_indices.emplace_back(chol.getColumn(0).getInt(), chol.getColumn(1).getInt());
            chol_values.push_back(chol.getColumn(2).getDouble());
        }
        ReconstructCholeskyFactor(n, chol_indices, chol_values, out.cholesky);

        if (out.cholesky.n != n || out.correlation.size() != n * n) {
            throw PersistenceError("TryLoadLatestHistoricalCalibrationEod: reconstructed matrix size mismatch.");
        }
        return out;
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"TryLoadLatestHistoricalCalibrationEod: "} + e.what());
    }
}

}  // namespace numeraire::database
