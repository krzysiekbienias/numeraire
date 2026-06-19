#include <SQLiteCpp/SQLiteCpp.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

[[nodiscard]] std::string ReadSchemaFile() {
    const fs::path schema = fs::path(NUMERAIRE_SOURCE_DIR) / "sql" / "schema_v1.sql";
    std::ifstream in(schema);
    if (!in) {
        throw std::runtime_error("failed to open schema: " + schema.string());
    }
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

[[nodiscard]] long InsertCalibrationHeader(SQLite::Database& db) {
    SQLite::Statement ins(
            db,
            "INSERT INTO historical_calibration (calibration_method, calibration_scope, scope_key, as_of, "
            "history_start, history_end, lookback_calendar_days, min_return_observations, "
            "vol_annualization_days, eod_adjusted, num_factors, num_return_observations, batch_run_id, "
            "source, remarks) "
            "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    ins.bind(1, "historical_eod_gbm");
    ins.bind(2, "book");
    ins.bind(3, "ALL");
    ins.bind(4, "2026-01-15");
    ins.bind(5, "2024-01-16");
    ins.bind(6, "2026-01-15");
    ins.bind(7, 504);
    ins.bind(8, 60);
    ins.bind(9, 252);
    ins.bind(10, 1);
    ins.bind(11, 2);
    ins.bind(12, 120);
    ins.bind(13, "cal-batch-001");
    ins.bind(14, "unit_test");
    ins.bind(15, "fixture");
    ins.exec();
    return db.getLastInsertRowid();
}

}  // namespace

TEST(HistoricalCalibrationSchemaTest, HeaderFactorsCorrelationAndCholeskyRoundTrip) {
    SQLite::Database db(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec("PRAGMA foreign_keys = ON;");
    db.exec(ReadSchemaFile());

    const long calibration_id = InsertCalibrationHeader(db);

    SQLite::Statement factor(
            db,
            "INSERT INTO historical_calibration_factor (calibration_id, factor_index, underlying_id, spot_as_of, "
            "volatility) VALUES (?,?,?,?,?)");
    factor.bind(1, calibration_id);
    factor.bind(2, 0);
    factor.bind(3, "AAPL");
    factor.bind(4, 190.0);
    factor.bind(5, 0.22);
    factor.exec();
    factor.reset();
    factor.bind(1, calibration_id);
    factor.bind(2, 1);
    factor.bind(3, "NDX");
    factor.bind(4, 21000.0);
    factor.bind(5, 0.18);
    factor.exec();

    SQLite::Statement corr(db,
                           "INSERT INTO historical_calibration_correlation (calibration_id, factor_i, factor_j, rho) "
                           "VALUES (?,?,?,?)");
    corr.bind(1, calibration_id);
    corr.bind(2, 0);
    corr.bind(3, 0);
    corr.bind(4, 1.0);
    corr.exec();
    corr.reset();
    corr.bind(1, calibration_id);
    corr.bind(2, 0);
    corr.bind(3, 1);
    corr.bind(4, 0.75);
    corr.exec();
    corr.reset();
    corr.bind(1, calibration_id);
    corr.bind(2, 1);
    corr.bind(3, 1);
    corr.bind(4, 1.0);
    corr.exec();

    SQLite::Statement chol(db,
                           "INSERT INTO historical_calibration_cholesky (calibration_id, row_i, col_j, l_value) "
                           "VALUES (?,?,?,?)");
    chol.bind(1, calibration_id);
    chol.bind(2, 0);
    chol.bind(3, 0);
    chol.bind(4, 1.0);
    chol.exec();
    chol.reset();
    chol.bind(1, calibration_id);
    chol.bind(2, 1);
    chol.bind(3, 0);
    chol.bind(4, 0.75);
    chol.exec();
    chol.reset();
    chol.bind(1, calibration_id);
    chol.bind(2, 1);
    chol.bind(3, 1);
    chol.bind(4, 0.66143783);
    chol.exec();

    SQLite::Statement latest(
            db,
            "SELECT num_factors, num_return_observations FROM historical_calibration "
            "WHERE calibration_scope = 'book' AND scope_key = 'ALL' AND as_of <= '2026-01-20' "
            "ORDER BY as_of DESC LIMIT 1");
    ASSERT_TRUE(latest.executeStep());
    EXPECT_EQ(latest.getColumn(0).getInt(), 2);
    EXPECT_EQ(latest.getColumn(1).getInt(), 120);

    SQLite::Statement factors(db,
                              "SELECT underlying_id, volatility FROM historical_calibration_factor "
                              "WHERE calibration_id = ? ORDER BY factor_index ASC");
    factors.bind(1, calibration_id);
    ASSERT_TRUE(factors.executeStep());
    EXPECT_EQ(factors.getColumn(0).getString(), std::string("AAPL"));
    EXPECT_DOUBLE_EQ(factors.getColumn(1).getDouble(), 0.22);
    ASSERT_TRUE(factors.executeStep());
    EXPECT_EQ(factors.getColumn(0).getString(), std::string("NDX"));

    db.exec("DELETE FROM historical_calibration WHERE calibration_id = " + std::to_string(calibration_id));

    SQLite::Statement orphan_check(db, "SELECT COUNT(*) FROM historical_calibration_factor");
    ASSERT_TRUE(orphan_check.executeStep());
    EXPECT_EQ(orphan_check.getColumn(0).getInt(), 0);
}

TEST(HistoricalCalibrationSchemaTest, RejectsDuplicateOfficialKey) {
    SQLite::Database db(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec("PRAGMA foreign_keys = ON;");
    db.exec(ReadSchemaFile());

    static_cast<void>(InsertCalibrationHeader(db));
    EXPECT_THROW({ static_cast<void>(InsertCalibrationHeader(db)); }, SQLite::Exception);
}
