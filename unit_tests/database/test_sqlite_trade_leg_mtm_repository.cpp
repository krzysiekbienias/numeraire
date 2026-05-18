#include <gtest/gtest.h>

#include <numeraire/database/sqlite_trade_leg_mtm_repository.hpp>
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
    fs::path const tpl = fs::temp_directory_path() / "numeraire_mtm_ut_XXXXXX";
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

TEST(SqliteTradeLegMtmRepositoryTest, UpsertInsertsRow) {
    std::string const path = TempSqlitePath();
    SeedMinimalTrade(path);

    numeraire::database::SqliteTradeLegMtmRepository repo(path);
    numeraire::database::TradeLegMtmEodRow row{};
    row.as_of = "2025-08-10";
    row.trade_id = "TRD_001";
    row.leg_id = "TRD_001_L1";
    row.batch_run_id = "batch-ut-1";
    row.underlying_spot = 240.0;
    row.risk_free_rate = 0.03;
    row.dividend_yield = 0.0;
    row.implied_vol_used = 0.20;
    row.years_to_maturity = 0.25;
    row.pv_unit = 5.5;
    row.pv_total = 550.0;
    row.delta = 0.5;
    row.gamma = 0.01;
    row.vega = 0.2;
    row.theta = -0.05;
    row.rho = 0.09;
    row.pricing_engine = "analytic_black_scholes";
    row.calculated_at = "2025-08-10T16:00:00Z";
    row.remarks = "ut";

    repo.Upsert(row);

    SQLite::Database check(path, SQLite::OPEN_READONLY);
    EXPECT_DOUBLE_EQ(
            QueryOneDouble(check, "SELECT COUNT(*) FROM trade_leg_mtm_eod WHERE leg_id='TRD_001_L1'"), 1.0);
    EXPECT_DOUBLE_EQ(
            QueryOneDouble(check,
                          "SELECT pv_total FROM trade_leg_mtm_eod WHERE leg_id='TRD_001_L1' "
                          "AND as_of='2025-08-10' AND pricing_engine='analytic_black_scholes'"),
            550.0);

    fs::remove(path);
}

TEST(SqliteTradeLegMtmRepositoryTest, UpsertAppendsArchiveOnEachRun) {
    std::string const path = TempSqlitePath();
    SeedMinimalTrade(path);

    numeraire::database::SqliteTradeLegMtmRepository repo(path);
    numeraire::database::TradeLegMtmEodRow row{};
    row.as_of = "2025-08-10";
    row.trade_id = "TRD_001";
    row.leg_id = "TRD_001_L1";
    row.batch_run_id = "batch-run-a";
    row.underlying_spot = 240.0;
    row.risk_free_rate = 0.03;
    row.dividend_yield = 0.0;
    row.implied_vol_used = 0.20;
    row.years_to_maturity = 0.25;
    row.pv_unit = 5.0;
    row.pv_total = 500.0;
    row.delta = 0.5;
    row.gamma = 0.01;
    row.vega = 0.2;
    row.theta = -0.05;
    row.rho = 0.09;
    row.pricing_engine = "analytic_black_scholes";
    row.calculated_at = "2025-08-10T16:00:00Z";
    row.remarks = "run-a";

    repo.Upsert(row);
    row.batch_run_id = "batch-run-b";
    row.pv_total = 600.0;
    row.remarks = "run-b";
    repo.Upsert(row);

    SQLite::Database check(path, SQLite::OPEN_READONLY);
    EXPECT_DOUBLE_EQ(QueryOneDouble(check, "SELECT COUNT(*) FROM trade_leg_mtm_eod WHERE leg_id='TRD_001_L1'"),
                     1.0);
    EXPECT_DOUBLE_EQ(
            QueryOneDouble(check, "SELECT COUNT(*) FROM trade_leg_mtm_eod_archive WHERE leg_id='TRD_001_L1'"),
            2.0);
    EXPECT_DOUBLE_EQ(
            QueryOneDouble(check,
                          "SELECT pv_total FROM trade_leg_mtm_eod WHERE leg_id='TRD_001_L1' "
                          "AND as_of='2025-08-10'"),
            600.0);

    fs::remove(path);
}

TEST(SqliteTradeLegMtmRepositoryTest, UpsertReplaceUpdatesSameUniqueKey) {
    std::string const path = TempSqlitePath();
    SeedMinimalTrade(path);

    numeraire::database::SqliteTradeLegMtmRepository repo(path);
    numeraire::database::TradeLegMtmEodRow row{};
    row.as_of = "2025-08-10";
    row.trade_id = "TRD_001";
    row.leg_id = "TRD_001_L1";
    row.underlying_spot = 240.0;
    row.risk_free_rate = 0.03;
    row.dividend_yield = 0.0;
    row.implied_vol_used = 0.20;
    row.years_to_maturity = 0.25;
    row.pv_unit = 5.0;
    row.pv_total = 500.0;
    row.delta = 0.5;
    row.gamma = 0.01;
    row.vega = 0.2;
    row.theta = -0.05;
    row.rho = 0.09;
    row.pricing_engine = "analytic_black_scholes";
    row.calculated_at = "2025-08-10T16:00:00Z";
    row.remarks = "first";

    repo.Upsert(row);
    row.pv_unit = 6.0;
    row.pv_total = 600.0;
    row.remarks = "second";
    repo.Upsert(row);

    SQLite::Database check(path, SQLite::OPEN_READONLY);
    EXPECT_DOUBLE_EQ(QueryOneDouble(check, "SELECT COUNT(*) FROM trade_leg_mtm_eod WHERE leg_id='TRD_001_L1'"),
                     1.0);
    EXPECT_DOUBLE_EQ(
            QueryOneDouble(check,
                          "SELECT pv_total FROM trade_leg_mtm_eod WHERE leg_id='TRD_001_L1' "
                          "AND as_of='2025-08-10' AND pricing_engine='analytic_black_scholes'"),
            600.0);

    fs::remove(path);
}

TEST(SqliteTradeLegMtmRepositoryTest, EmptyRequiredFieldsThrows) {
    std::string const path = TempSqlitePath();
    SeedMinimalTrade(path);
    numeraire::database::SqliteTradeLegMtmRepository repo(path);
    numeraire::database::TradeLegMtmEodRow row{};
    row.as_of = "2025-08-10";
    row.trade_id = "TRD_001";
    row.leg_id = "TRD_001_L1";
    row.pricing_engine = "";
    EXPECT_THROW({ repo.Upsert(row); }, numeraire::ValidationError);
    fs::remove(path);
}
