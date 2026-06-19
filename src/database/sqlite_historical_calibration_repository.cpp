#include <numeraire/database/sqlite_historical_calibration_repository.hpp>

#include <numeraire/utils/exception.hpp>

#include <SQLiteCpp/SQLiteCpp.h>

namespace numeraire::database {

SqliteHistoricalCalibrationRepository::SqliteHistoricalCalibrationRepository(std::string database_file_path)
    : database_file_path_(std::move(database_file_path)) {}

long SqliteHistoricalCalibrationRepository::UpsertSnapshot(
        const HistoricalCalibrationHeaderWrite& header,
        const std::vector<HistoricalCalibrationFactorWrite>& factors,
        const std::vector<HistoricalCalibrationCorrelationWrite>& correlations,
        const std::vector<HistoricalCalibrationCholeskyWrite>& cholesky) {
    try {
        SQLite::Database db(database_file_path_, SQLite::OPEN_READWRITE);
        db.exec("PRAGMA foreign_keys = ON");
        SQLite::Transaction txn(db);

        SQLite::Statement del(
                db,
                "DELETE FROM historical_calibration WHERE calibration_scope = ? AND scope_key = ? AND as_of = ? "
                "AND calibration_method = ?");
        del.bind(1, header.calibration_scope);
        del.bind(2, header.scope_key);
        del.bind(3, header.as_of);
        del.bind(4, header.calibration_method);
        del.exec();

        SQLite::Statement ins_header(
                db,
                "INSERT INTO historical_calibration ("
                "calibration_method, calibration_scope, scope_key, as_of, history_start, history_end, "
                "lookback_calendar_days, min_return_observations, vol_annualization_days, eod_adjusted, "
                "num_factors, num_return_observations, batch_run_id, source, remarks"
                ") VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
        ins_header.bind(1, header.calibration_method);
        ins_header.bind(2, header.calibration_scope);
        ins_header.bind(3, header.scope_key);
        ins_header.bind(4, header.as_of);
        ins_header.bind(5, header.history_start);
        ins_header.bind(6, header.history_end);
        ins_header.bind(7, header.lookback_calendar_days);
        ins_header.bind(8, header.min_return_observations);
        ins_header.bind(9, header.vol_annualization_days);
        ins_header.bind(10, header.eod_adjusted);
        ins_header.bind(11, header.num_factors);
        ins_header.bind(12, header.num_return_observations);
        ins_header.bind(13, header.batch_run_id);
        ins_header.bind(14, header.source);
        ins_header.bind(15, header.remarks);
        ins_header.exec();

        const long calibration_id = db.getLastInsertRowid();

        SQLite::Statement ins_factor(
                db,
                "INSERT INTO historical_calibration_factor (calibration_id, factor_index, underlying_id, spot_as_of, "
                "volatility) VALUES (?,?,?,?,?)");
        for (const HistoricalCalibrationFactorWrite& factor : factors) {
            ins_factor.bind(1, calibration_id);
            ins_factor.bind(2, factor.factor_index);
            ins_factor.bind(3, factor.underlying_id);
            ins_factor.bind(4, factor.spot_as_of);
            ins_factor.bind(5, factor.volatility);
            ins_factor.exec();
            ins_factor.reset();
        }

        SQLite::Statement ins_corr(
                db,
                "INSERT INTO historical_calibration_correlation (calibration_id, factor_i, factor_j, rho) "
                "VALUES (?,?,?,?)");
        for (const HistoricalCalibrationCorrelationWrite& corr : correlations) {
            ins_corr.bind(1, calibration_id);
            ins_corr.bind(2, corr.factor_i);
            ins_corr.bind(3, corr.factor_j);
            ins_corr.bind(4, corr.rho);
            ins_corr.exec();
            ins_corr.reset();
        }

        SQLite::Statement ins_chol(
                db,
                "INSERT INTO historical_calibration_cholesky (calibration_id, row_i, col_j, l_value) VALUES (?,?,?,?)");
        for (const HistoricalCalibrationCholeskyWrite& chol : cholesky) {
            ins_chol.bind(1, calibration_id);
            ins_chol.bind(2, chol.row_i);
            ins_chol.bind(3, chol.col_j);
            ins_chol.bind(4, chol.l_value);
            ins_chol.exec();
            ins_chol.reset();
        }

        txn.commit();
        return calibration_id;
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"SqliteHistoricalCalibrationRepository::UpsertSnapshot: "} + e.what());
    }
}

}  // namespace numeraire::database
