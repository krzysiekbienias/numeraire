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

[[nodiscard]] long InsertHistoricalGbmHeader(SQLite::Database& db,
                                             const std::string& model = "gbm",
                                             const std::string& source = "historical") {
    SQLite::Statement ins(
            db,
            "INSERT INTO calibration_snapshot (model, source, calibration_scope, scope_key, as_of, num_factors, "
            "history_start, history_end, lookback_calendar_days, min_return_observations, "
            "vol_annualization_days, eod_adjusted, num_return_observations, batch_run_id, "
            "produced_by, remarks) "
            "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    ins.bind(1, model);
    ins.bind(2, source);
    ins.bind(3, "book");
    ins.bind(4, "ALL");
    ins.bind(5, "2026-01-15");
    ins.bind(6, 2);
    ins.bind(7, "2024-01-16");
    ins.bind(8, "2026-01-15");
    ins.bind(9, 504);
    ins.bind(10, 60);
    ins.bind(11, 252);
    ins.bind(12, 1);
    ins.bind(13, 120);
    ins.bind(14, "cal-batch-001");
    ins.bind(15, "unit_test");
    ins.bind(16, "fixture");
    ins.exec();
    return db.getLastInsertRowid();
}

[[nodiscard]] SQLite::Database MakeSchemaDb() {
    SQLite::Database db(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec("PRAGMA foreign_keys = ON;");
    db.exec(ReadSchemaFile());
    return db;
}

}  // namespace

TEST(CalibrationSchemaTest, HeaderFactorsCorrelationAndCholeskyRoundTrip) {
    SQLite::Database db = MakeSchemaDb();

    const long calibration_id = InsertHistoricalGbmHeader(db);

    SQLite::Statement factor(
            db,
            "INSERT INTO calibration_factor (calibration_id, factor_index, factor_id, factor_level, "
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
                           "INSERT INTO calibration_correlation (calibration_id, factor_i, factor_j, rho) "
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
                           "INSERT INTO calibration_cholesky (calibration_id, row_i, col_j, l_value) "
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
            "SELECT num_factors, num_return_observations FROM calibration_snapshot "
            "WHERE calibration_scope = 'book' AND scope_key = 'ALL' AND as_of <= '2026-01-20' "
            "ORDER BY as_of DESC LIMIT 1");
    ASSERT_TRUE(latest.executeStep());
    EXPECT_EQ(latest.getColumn(0).getInt(), 2);
    EXPECT_EQ(latest.getColumn(1).getInt(), 120);

    SQLite::Statement factors(db,
                              "SELECT factor_id, volatility FROM calibration_factor "
                              "WHERE calibration_id = ? ORDER BY factor_index ASC");
    factors.bind(1, calibration_id);
    ASSERT_TRUE(factors.executeStep());
    EXPECT_EQ(factors.getColumn(0).getString(), std::string("AAPL"));
    EXPECT_DOUBLE_EQ(factors.getColumn(1).getDouble(), 0.22);
    ASSERT_TRUE(factors.executeStep());
    EXPECT_EQ(factors.getColumn(0).getString(), std::string("NDX"));

    db.exec("DELETE FROM calibration_snapshot WHERE calibration_id = " + std::to_string(calibration_id));

    SQLite::Statement orphan_check(db, "SELECT COUNT(*) FROM calibration_factor");
    ASSERT_TRUE(orphan_check.executeStep());
    EXPECT_EQ(orphan_check.getColumn(0).getInt(), 0);
}

TEST(CalibrationSchemaTest, RejectsDuplicateOfficialKey) {
    SQLite::Database db = MakeSchemaDb();

    static_cast<void>(InsertHistoricalGbmHeader(db));
    EXPECT_THROW({ static_cast<void>(InsertHistoricalGbmHeader(db)); }, SQLite::Exception);
}

TEST(CalibrationSchemaTest, SameScopeAndDateHoldsOneSnapshotPerModelAndSource) {
    SQLite::Database db = MakeSchemaDb();

    static_cast<void>(InsertHistoricalGbmHeader(db, "gbm", "historical"));
    static_cast<void>(InsertHistoricalGbmHeader(db, "gabillon_2f", "historical"));

    SQLite::Statement implied(db,
                              "INSERT INTO calibration_snapshot (model, source, calibration_scope, scope_key, "
                              "as_of, num_factors) VALUES ('gabillon_2f', 'implied', 'book', 'ALL', "
                              "'2026-01-15', 4)");
    implied.exec();

    SQLite::Statement count(db,
                            "SELECT COUNT(*) FROM calibration_snapshot "
                            "WHERE calibration_scope = 'book' AND scope_key = 'ALL' AND as_of = '2026-01-15'");
    ASSERT_TRUE(count.executeStep());
    EXPECT_EQ(count.getColumn(0).getInt(), 3);
}

TEST(CalibrationSchemaTest, HistoricalSnapshotMustCarryItsHistoryWindow) {
    SQLite::Database db = MakeSchemaDb();

    EXPECT_THROW(
            {
                db.exec("INSERT INTO calibration_snapshot (model, source, calibration_scope, scope_key, as_of, "
                        "num_factors) VALUES ('gbm', 'historical', 'book', 'ALL', '2026-01-15', 2)");
            },
            SQLite::Exception);
}

TEST(CalibrationSchemaTest, StoresModelSpecificParamsAndCascades) {
    SQLite::Database db = MakeSchemaDb();

    db.exec("INSERT INTO calibration_snapshot (model, source, calibration_scope, scope_key, as_of, num_factors) "
            "VALUES ('gabillon_2f', 'implied', 'underlying', 'CL', '2026-01-15', 2)");
    const long calibration_id = db.getLastInsertRowid();

    // Long-end factor has no observable level, so `factor_level` stays NULL.
    db.exec("INSERT INTO calibration_factor (calibration_id, factor_index, factor_id, factor_level, volatility) "
            "VALUES (" +
            std::to_string(calibration_id) +
            ", 0, 'CL_SHORT', 62.5, 0.35), (" + std::to_string(calibration_id) +
            ", 1, 'CL_LONG', NULL, 0.18)");
    db.exec("INSERT INTO calibration_param (calibration_id, factor_id, param_name, param_value) VALUES (" +
            std::to_string(calibration_id) + ", 'CL', 'kappa', 1.25)");

    SQLite::Statement kappa(db,
                            "SELECT param_value FROM calibration_param "
                            "WHERE calibration_id = ? AND factor_id = 'CL' AND param_name = 'kappa'");
    kappa.bind(1, calibration_id);
    ASSERT_TRUE(kappa.executeStep());
    EXPECT_DOUBLE_EQ(kappa.getColumn(0).getDouble(), 1.25);

    SQLite::Statement long_level(db,
                                 "SELECT factor_level IS NULL FROM calibration_factor "
                                 "WHERE calibration_id = ? AND factor_id = 'CL_LONG'");
    long_level.bind(1, calibration_id);
    ASSERT_TRUE(long_level.executeStep());
    EXPECT_EQ(long_level.getColumn(0).getInt(), 1);

    db.exec("DELETE FROM calibration_snapshot WHERE calibration_id = " + std::to_string(calibration_id));
    SQLite::Statement orphan(db, "SELECT COUNT(*) FROM calibration_param");
    ASSERT_TRUE(orphan.executeStep());
    EXPECT_EQ(orphan.getColumn(0).getInt(), 0);
}
