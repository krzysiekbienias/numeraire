#include <numeraire/database/sqlite_calibration_repository.hpp>

#include <numeraire/utils/exception.hpp>

#include <SQLiteCpp/SQLiteCpp.h>

#include <optional>
#include <string>

namespace numeraire::database {
namespace {

template <typename T>
void BindOptional(SQLite::Statement& st, const int index, const std::optional<T>& value) {
    if (value.has_value()) {
        st.bind(index, *value);
    } else {
        st.bind(index);
    }
}

}  // namespace

SqliteCalibrationRepository::SqliteCalibrationRepository(std::string database_file_path)
    : database_file_path_(std::move(database_file_path)) {}

long SqliteCalibrationRepository::UpsertSnapshot(const CalibrationHeaderWrite& header,
                                                 const std::vector<CalibrationFactorWrite>& factors,
                                                 const std::vector<CalibrationCorrelationWrite>& correlations,
                                                 const std::vector<CalibrationCholeskyWrite>& cholesky,
                                                 const std::vector<CalibrationParamWrite>& params) {
    try {
        SQLite::Database db(database_file_path_, SQLite::OPEN_READWRITE);
        db.exec("PRAGMA foreign_keys = ON");
        SQLite::Transaction txn(db);

        SQLite::Statement del(
                db,
                "DELETE FROM calibration_snapshot WHERE calibration_scope = ? AND scope_key = ? AND as_of = ? "
                "AND model = ? AND source = ?");
        del.bind(1, header.calibration_scope);
        del.bind(2, header.scope_key);
        del.bind(3, header.as_of);
        del.bind(4, header.model);
        del.bind(5, header.source);
        del.exec();

        SQLite::Statement ins_header(
                db,
                "INSERT INTO calibration_snapshot ("
                "model, source, calibration_scope, scope_key, as_of, num_factors, "
                "history_start, history_end, lookback_calendar_days, min_return_observations, "
                "vol_annualization_days, eod_adjusted, num_return_observations, batch_run_id, "
                "produced_by, remarks"
                ") VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
        ins_header.bind(1, header.model);
        ins_header.bind(2, header.source);
        ins_header.bind(3, header.calibration_scope);
        ins_header.bind(4, header.scope_key);
        ins_header.bind(5, header.as_of);
        ins_header.bind(6, header.num_factors);
        BindOptional(ins_header, 7, header.history_start);
        BindOptional(ins_header, 8, header.history_end);
        BindOptional(ins_header, 9, header.lookback_calendar_days);
        BindOptional(ins_header, 10, header.min_return_observations);
        BindOptional(ins_header, 11, header.vol_annualization_days);
        BindOptional(ins_header, 12, header.eod_adjusted);
        BindOptional(ins_header, 13, header.num_return_observations);
        ins_header.bind(14, header.batch_run_id);
        ins_header.bind(15, header.produced_by);
        ins_header.bind(16, header.remarks);
        ins_header.exec();

        const long calibration_id = db.getLastInsertRowid();

        SQLite::Statement ins_factor(
                db,
                "INSERT INTO calibration_factor (calibration_id, factor_index, factor_id, factor_level, "
                "volatility) VALUES (?,?,?,?,?)");
        for (const CalibrationFactorWrite& factor : factors) {
            ins_factor.bind(1, calibration_id);
            ins_factor.bind(2, factor.factor_index);
            ins_factor.bind(3, factor.factor_id);
            BindOptional(ins_factor, 4, factor.factor_level);
            ins_factor.bind(5, factor.volatility);
            ins_factor.exec();
            ins_factor.reset();
        }

        SQLite::Statement ins_corr(
                db,
                "INSERT INTO calibration_correlation (calibration_id, factor_i, factor_j, rho) "
                "VALUES (?,?,?,?)");
        for (const CalibrationCorrelationWrite& corr : correlations) {
            ins_corr.bind(1, calibration_id);
            ins_corr.bind(2, corr.factor_i);
            ins_corr.bind(3, corr.factor_j);
            ins_corr.bind(4, corr.rho);
            ins_corr.exec();
            ins_corr.reset();
        }

        SQLite::Statement ins_chol(
                db,
                "INSERT INTO calibration_cholesky (calibration_id, row_i, col_j, l_value) VALUES (?,?,?,?)");
        for (const CalibrationCholeskyWrite& chol : cholesky) {
            ins_chol.bind(1, calibration_id);
            ins_chol.bind(2, chol.row_i);
            ins_chol.bind(3, chol.col_j);
            ins_chol.bind(4, chol.l_value);
            ins_chol.exec();
            ins_chol.reset();
        }

        SQLite::Statement ins_param(
                db,
                "INSERT INTO calibration_param (calibration_id, factor_id, param_name, param_value) "
                "VALUES (?,?,?,?)");
        for (const CalibrationParamWrite& param : params) {
            ins_param.bind(1, calibration_id);
            ins_param.bind(2, param.factor_id);
            ins_param.bind(3, param.param_name);
            ins_param.bind(4, param.param_value);
            ins_param.exec();
            ins_param.reset();
        }

        txn.commit();
        return calibration_id;
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"SqliteCalibrationRepository::UpsertSnapshot: "} + e.what());
    }
}

}  // namespace numeraire::database
