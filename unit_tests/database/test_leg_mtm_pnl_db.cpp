#include <gtest/gtest.h>

#include <numeraire/database/leg_mtm_pnl.hpp>
#include <numeraire/database/leg_pv.hpp>
#include <numeraire/database/sqlite_trade_leg_mtm_repository.hpp>
#include <numeraire/database/sqlite_trade_repository.hpp>
#include <numeraire/enums/position_direction.hpp>

#include <SQLiteCpp/SQLiteCpp.h>

#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#include <filesystem>

namespace fs = std::filesystem;

namespace {

constexpr const char* kLegId = "TRD_001_L1";
constexpr const char* kTradeId = "TRD_001";
constexpr const char* kPricingEngine = "analytic_black_scholes";

[[nodiscard]] std::string TempSqlitePath() {
    fs::path const tpl = fs::temp_directory_path() / "numeraire_mtm_pnl_ut_XXXXXX";
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

void SeedMinimalTrade(const std::string& db_path, const double commission = 0.0) {
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
    SQLite::Statement leg(
            db,
            "INSERT INTO trade_legs (leg_id, trade_id, product_id, direction, quantity, "
            "execution_price, commission) VALUES (?, ?, ?, ?, ?, ?, ?)");
    leg.bind(1, kLegId);
    leg.bind(2, kTradeId);
    leg.bind(3, "P_AAPL_001");
    leg.bind(4, "LONG");
    leg.bind(5, 100.0);
    leg.bind(6, 12.5);
    leg.bind(7, commission);
    leg.exec();
}

[[nodiscard]] std::optional<double> QueryPnlColumn(SQLite::Database& db,
                                                   const char* column,
                                                   const char* as_of) {
    std::ostringstream sql;
    sql << "SELECT " << column << " FROM trade_leg_mtm_eod WHERE leg_id='" << kLegId << "' AND as_of='"
        << as_of << "' AND pricing_engine='" << kPricingEngine << "'";
    SQLite::Statement st(db, sql.str());
    if (!st.executeStep()) {
        return std::nullopt;
    }
    if (st.getColumn(0).isNull()) {
        return std::nullopt;
    }
    return st.getColumn(0).getDouble();
}

void FillPositionGreekTotals(numeraire::database::TradeLegMtmEodRow& row) {
    constexpr numeraire::PositionDirection direction = numeraire::PositionDirection::kLong;
    constexpr double quantity = 100.0;
    constexpr double contract_size = 100.0;

    row.delta_total =
            numeraire::database::LegDeltaTotal(direction, quantity, contract_size, row.delta);
    row.gamma_total =
            numeraire::database::LegGammaTotal(direction, quantity, contract_size, row.gamma);
    row.vega_total = numeraire::database::LegVegaTotal(direction, quantity, contract_size, row.vega);
    row.theta_total =
            numeraire::database::LegThetaTotal(direction, quantity, contract_size, row.theta);
    row.rho_total = numeraire::database::LegRhoTotal(direction, quantity, contract_size, row.rho);
}

[[nodiscard]] numeraire::database::TradeLegMtmEodRow MakeMtmRow(const char* as_of,
                                                              const char* batch_run_id,
                                                              const double pv_total) {
    numeraire::database::TradeLegMtmEodRow row{};
    row.as_of = as_of;
    row.trade_id = kTradeId;
    row.leg_id = kLegId;
    row.batch_run_id = batch_run_id;
    row.underlying_spot = 240.0;
    row.risk_free_rate = 0.03;
    row.dividend_yield = 0.0;
    row.implied_vol_used = 0.20;
    row.years_to_maturity = 0.25;
    row.pv_unit = pv_total / 10000.0;
    row.pv_total = pv_total;
    row.delta = 0.5;
    row.gamma = 0.01;
    row.vega = 0.2;
    row.theta = -0.05;
    row.rho = 0.09;
    FillPositionGreekTotals(row);
    row.pricing_engine = kPricingEngine;
    row.remarks = "pnl-db-ut";
    return row;
}

/// Mirrors `BookNpvAndPersistMtmForBundle`: lookup prior from DB, compute PnL from catalog leg.
void PersistMtmWithPnl(numeraire::database::SqliteTradeLegMtmRepository& mtm_repo,
                       const numeraire::database::TradeLegCatalogRow& catalog_row,
                       numeraire::database::TradeLegMtmEodRow row) {
    const double booked_mark = numeraire::database::LegBookedMark(catalog_row);
    const std::optional prior_official =
            mtm_repo.LookupPriorOfficialMark(row.leg_id, row.pricing_engine, row.as_of);
    const double pv_total_prev =
            numeraire::database::ResolvePvTotalPrevForDaily(prior_official, booked_mark);
    const double commission = numeraire::database::LegCommissionOrZero(catalog_row.leg);
    row.pnl_daily = numeraire::database::LegPnlDaily(row.pv_total, pv_total_prev);
    row.pnl_inception =
            numeraire::database::LegPnlInception(row.pv_total, booked_mark, commission);
    mtm_repo.Upsert(row);
}

[[nodiscard]] const numeraire::database::TradeLegCatalogRow& FindLeg(
        const numeraire::database::TradeCatalogBundle& bundle) {
    for (const auto& leg : bundle.legs) {
        if (leg.leg.leg_id == kLegId) {
            return leg;
        }
    }
    throw std::runtime_error("leg not found in catalog bundle");
}

}  // namespace

TEST(LegMtmPnlDbTest, FirstEodPersistsPnlFromBookedMark) {
    std::string const path = TempSqlitePath();
    SeedMinimalTrade(path);

    numeraire::database::SqliteTradeRepository trade_repo(path);
    const auto bundle = trade_repo.GetCatalogForTrade(kTradeId);
    const auto& catalog_row = FindLeg(bundle);

    numeraire::database::SqliteTradeLegMtmRepository mtm_repo(path);
    PersistMtmWithPnl(mtm_repo, catalog_row, MakeMtmRow("2025-08-08", "batch-day1", 130000.0));

    SQLite::Database check(path, SQLite::OPEN_READONLY);
    const std::optional pnl_daily = QueryPnlColumn(check, "pnl_daily", "2025-08-08");
    const std::optional pnl_inception = QueryPnlColumn(check, "pnl_inception", "2025-08-08");
    ASSERT_TRUE(pnl_daily.has_value());
    ASSERT_TRUE(pnl_inception.has_value());
    EXPECT_DOUBLE_EQ(*pnl_daily, 5000.0);
    EXPECT_DOUBLE_EQ(*pnl_inception, 5000.0);

    fs::remove(path);
}

TEST(LegMtmPnlDbTest, SecondEodUsesPriorOfficialMarkForDailyPnl) {
    std::string const path = TempSqlitePath();
    SeedMinimalTrade(path);

    numeraire::database::SqliteTradeRepository trade_repo(path);
    const auto bundle = trade_repo.GetCatalogForTrade(kTradeId);
    const auto& catalog_row = FindLeg(bundle);

    numeraire::database::SqliteTradeLegMtmRepository mtm_repo(path);
    PersistMtmWithPnl(mtm_repo, catalog_row, MakeMtmRow("2025-08-08", "batch-day1", 130000.0));
    PersistMtmWithPnl(mtm_repo, catalog_row, MakeMtmRow("2025-08-09", "batch-day2", 132500.0));

    SQLite::Database check(path, SQLite::OPEN_READONLY);
    const std::optional day1_daily = QueryPnlColumn(check, "pnl_daily", "2025-08-08");
    const std::optional day2_daily = QueryPnlColumn(check, "pnl_daily", "2025-08-09");
    const std::optional day2_inception = QueryPnlColumn(check, "pnl_inception", "2025-08-09");
    ASSERT_TRUE(day1_daily.has_value());
    ASSERT_TRUE(day2_daily.has_value());
    ASSERT_TRUE(day2_inception.has_value());
    EXPECT_DOUBLE_EQ(*day1_daily, 5000.0);
    EXPECT_DOUBLE_EQ(*day2_daily, 2500.0);
    EXPECT_DOUBLE_EQ(*day2_inception, 7500.0);

    fs::remove(path);
}

TEST(LegMtmPnlDbTest, GapDaysDailyPnlSpansMissingCalendarDays) {
    std::string const path = TempSqlitePath();
    SeedMinimalTrade(path);

    numeraire::database::SqliteTradeRepository trade_repo(path);
    const auto bundle = trade_repo.GetCatalogForTrade(kTradeId);
    const auto& catalog_row = FindLeg(bundle);

    numeraire::database::SqliteTradeLegMtmRepository mtm_repo(path);
    PersistMtmWithPnl(mtm_repo, catalog_row, MakeMtmRow("2025-08-08", "batch-fri", 130000.0));
    PersistMtmWithPnl(mtm_repo, catalog_row, MakeMtmRow("2025-08-11", "batch-mon", 133000.0));

    SQLite::Database check(path, SQLite::OPEN_READONLY);
    const std::optional pnl_daily = QueryPnlColumn(check, "pnl_daily", "2025-08-11");
    ASSERT_TRUE(pnl_daily.has_value());
    EXPECT_DOUBLE_EQ(*pnl_daily, 3000.0);

    fs::remove(path);
}

TEST(LegMtmPnlDbTest, InceptionSubtractsCommissionFromTradeLegs) {
    std::string const path = TempSqlitePath();
    SeedMinimalTrade(path, /*commission=*/500.0);

    numeraire::database::SqliteTradeRepository trade_repo(path);
    const auto bundle = trade_repo.GetCatalogForTrade(kTradeId);
    const auto& catalog_row = FindLeg(bundle);

    numeraire::database::SqliteTradeLegMtmRepository mtm_repo(path);
    PersistMtmWithPnl(mtm_repo, catalog_row, MakeMtmRow("2025-08-08", "batch-comm", 130000.0));

    SQLite::Database check(path, SQLite::OPEN_READONLY);
    const std::optional pnl_daily = QueryPnlColumn(check, "pnl_daily", "2025-08-08");
    const std::optional pnl_inception = QueryPnlColumn(check, "pnl_inception", "2025-08-08");
    ASSERT_TRUE(pnl_daily.has_value());
    ASSERT_TRUE(pnl_inception.has_value());
    EXPECT_DOUBLE_EQ(*pnl_daily, 5000.0);
    EXPECT_DOUBLE_EQ(*pnl_inception, 4500.0);

    fs::remove(path);
}
