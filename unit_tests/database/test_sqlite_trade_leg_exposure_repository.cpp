#include <gtest/gtest.h>

#include <numeraire/database/sqlite_trade_leg_exposure_repository.hpp>
#include <numeraire/utils/exception.hpp>

#include <SQLiteCpp/SQLiteCpp.h>

#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace {

[[nodiscard]] std::string TempSqlitePath() {
    fs::path const tpl = fs::temp_directory_path() / "numeraire_exposure_ut_XXXXXX";
    std::string p = tpl.string();
    std::vector<char> buf(p.begin(), p.end());
    buf.push_back('\0');
    const int fd = mkstemp(buf.data());
    if (fd < 0) {
        throw std::runtime_error("mkstemp failed for temp sqlite path");
    }
    close(fd);
    return std::string(buf.data());
}

[[nodiscard]] std::string ReadSchemaFile() {
    fs::path const schema = fs::path(NUMERAIRE_SOURCE_DIR) / "sql" / "schema_v1.sql";
    std::ifstream in(schema);
    if (!in) {
        throw std::runtime_error("failed to open schema: " + schema.string());
    }
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

void SeedMinimalTrade(const std::string& db_path) {
    SQLite::Database db(db_path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec(ReadSchemaFile());
    db.exec(
            "INSERT INTO products (product_id, asset_kind, underlying_id, expiry_date, settlement, "
            "currency, contract_size, day_count, calendar) VALUES "
            "('P_AAPL_001', 'EQUITY', 'AAPL', '2025-11-04', 'PHYSICAL', "
            "'USD', 100.0, 'Actual365Fixed', 'UnitedStates');");
    db.exec(
            "INSERT INTO products_equity (product_id, instrument_type, option_type, strike, "
            "exercise_style, structured_params) VALUES "
            "('P_AAPL_001', 'plain_vanilla_european_option', 'call', 233, 'european', '{}');");
    db.exec(
            "INSERT INTO trades (trade_id, portfolio_id, strategy_type, booking_timestamp, trade_date, "
            "updated_at, status) VALUES "
            "('TRD_001', 'BOOK_1', 'VANILLA_OPTION', '2025-08-04 10:00:00', '2025-08-06', "
            "NULL, 'LIVE');");
    db.exec(
            "INSERT INTO trade_legs (leg_id, trade_id, product_id, direction, quantity, "
            "execution_price, commission) VALUES "
            "('TRD_001_L1', 'TRD_001', 'P_AAPL_001', 'LONG', 100, 12.5, 0);");
}

[[nodiscard]] double QueryOneDouble(SQLite::Database& db, const char* sql) {
    SQLite::Statement st(db, sql);
    if (!st.executeStep()) {
        throw std::runtime_error("expected one row");
    }
    return st.getColumn(0).getDouble();
}

}  // namespace

TEST(SqliteTradeLegExposureRepositoryTest, UpsertOfficialAndArchive) {
    const std::string db_path = TempSqlitePath();
    SeedMinimalTrade(db_path);

    numeraire::database::SqliteTradeLegExposureRepository repo(db_path);
    numeraire::database::TradeLegExposureEodRow row{};
    row.as_of = "2026-06-15";
    row.trade_id = "TRD_001";
    row.leg_id = "TRD_001_L1";
    row.pillar_id = "M1";
    row.grid_step = 1;
    row.year_fraction = 0.082191780821917804;
    row.exposure_date = "2026-07-15";
    row.ee = 1000.0;
    row.pfe_95 = 2000.0;
    row.pfe_97 = 2500.0;
    row.num_paths = 1000;
    row.mc_seed = 42;
    row.calibration_id = 3;
    row.scope_key = "BOOK_1";
    row.batch_run_id = "exposure-test-batch";
    row.pricing_engine = "analytic_bs_path";
    row.remarks = "IV_DB;R_DB;Q_ENV";

    repo.Upsert(row);

    SQLite::Database check(db_path, SQLite::OPEN_READONLY);
    EXPECT_DOUBLE_EQ(
            QueryOneDouble(check,
                           "SELECT COUNT(*) FROM trade_leg_exposure_eod WHERE leg_id='TRD_001_L1' AND pillar_id='M1'"),
            1.0);
    EXPECT_DOUBLE_EQ(
            QueryOneDouble(check, "SELECT ee FROM trade_leg_exposure_eod WHERE leg_id='TRD_001_L1' AND pillar_id='M1'"),
            1000.0);
    EXPECT_DOUBLE_EQ(
            QueryOneDouble(check,
                           "SELECT COUNT(*) FROM trade_leg_exposure_eod_archive WHERE batch_run_id='exposure-test-batch'"),
            1.0);

    row.ee = 1100.0;
    repo.Upsert(row);
    EXPECT_DOUBLE_EQ(
            QueryOneDouble(check, "SELECT ee FROM trade_leg_exposure_eod WHERE leg_id='TRD_001_L1' AND pillar_id='M1'"),
            1100.0);
    EXPECT_DOUBLE_EQ(
            QueryOneDouble(check,
                           "SELECT COUNT(*) FROM trade_leg_exposure_eod_archive WHERE batch_run_id='exposure-test-batch'"),
            2.0);

    fs::remove(db_path);
}
