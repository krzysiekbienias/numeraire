#include <SQLiteCpp/SQLiteCpp.h>
#include <gtest/gtest.h>

#include <numeraire/schedule/date.hpp>
#include <numeraire/schedule/format_iso_date.hpp>
#include <numeraire/simulation/historical_calibrator.hpp>
#include <numeraire/utils/exception.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

using numeraire::schedule::AddCalendarDays;
using numeraire::schedule::FormatIsoDate;
using numeraire::schedule::ParseIsoDate;
using numeraire::simulation::CalibrateBookFromDatabase;
using numeraire::simulation::HistoricalCalibratorConfig;

constexpr int kNumSessions = 90;

[[nodiscard]] std::string ReadSchemaFile() {
    const fs::path schema = fs::path(NUMERAIRE_SOURCE_DIR) / "sql" / "schema_v1.sql";
    std::ifstream in(schema);
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

[[nodiscard]] fs::path UniqueSqlitePath() {
    using namespace std::chrono;
    const auto ns = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    return fs::temp_directory_path() / ("numeraire_book_factor_ut_" + std::to_string(ns) + ".sqlite3");
}

void InsertEquityBar(SQLite::Database& db, const std::string& ticker, const std::string& as_of, double close) {
    SQLite::Statement ins(
            db,
            "INSERT INTO equity_daily_eod (ticker, as_of, session_calendar, open, high, low, close, currency, "
            "source, timespan, adjusted, ingested_at) VALUES (?,?,'America/New_York',?,?,?,?,'USD','ut','1d',1,"
            "'2026-01-01T00:00:00Z')");
    ins.bind(1, ticker);
    ins.bind(2, as_of);
    ins.bind(3, close);
    ins.bind(4, close);
    ins.bind(5, close);
    ins.bind(6, close);
    ins.exec();
}

void InsertFuturesContract(SQLite::Database& db, const std::string& ticker, const std::string& settlement_date) {
    SQLite::Statement ins(db,
                          "INSERT INTO futures_contract (ticker, listing_as_of, product_code, settlement_date, "
                          "source, ingested_at) VALUES (?, '2025-01-01', 'CL', ?, 'ut', '2026-01-01T00:00:00Z')");
    ins.bind(1, ticker);
    ins.bind(2, settlement_date);
    ins.exec();
}

void InsertFuturesSettle(SQLite::Database& db, const std::string& ticker, const std::string& as_of, double settle) {
    SQLite::Statement ins(db,
                          "INSERT INTO futures_daily_eod (ticker, as_of, open, high, low, close, settlement_price, "
                          "source, ingested_at) VALUES (?,?,?,?,?,?,?,'ut','2026-01-01T00:00:00Z')");
    ins.bind(1, ticker);
    ins.bind(2, as_of);
    ins.bind(3, settle);
    ins.bind(4, settle);
    ins.bind(5, settle);
    ins.bind(6, settle);
    ins.bind(7, settle);
    ins.exec();
}

/// One equity leg and one commodity futures leg, both LIVE in BOOK_MIX.
void SeedMixedBook(SQLite::Database& db) {
    db.exec(
            "INSERT INTO products (product_id, asset_kind, underlying_id, expiry_date, settlement, currency, "
            "contract_size, day_count, calendar) VALUES "
            "('P_AAPL', 'EQUITY', 'AAPL', '2026-12-31', 'PHYSICAL', 'USD', 100.0, 'Actual365Fixed', "
            "'UnitedStates'), "
            "('P_CL', 'COMMODITY', 'CL', '2025-06-01', 'PHYSICAL', 'USD', 1000.0, 'Actual365Fixed', "
            "'UnitedStates');");
    db.exec(
            "INSERT INTO products_equity (product_id, instrument_type, option_type, strike, exercise_style, "
            "structured_params) VALUES "
            "('P_AAPL', 'plain_vanilla_european_option', 'call', 100, 'european', '{}');");
    db.exec(
            "INSERT INTO trades (trade_id, portfolio_id, strategy_type, booking_timestamp, trade_date, updated_at, "
            "status) VALUES "
            "('TRD_EQ', 'BOOK_MIX', 'VANILLA_OPTION', '2025-01-01 10:00:00', '2025-01-02', '2025-01-02', 'LIVE'), "
            "('TRD_CM', 'BOOK_MIX', 'COMMODITY_FUTURES', '2025-01-01 10:00:00', '2025-01-02', '2025-01-02', "
            "'LIVE');");
    db.exec(
            "INSERT INTO trade_legs (leg_id, trade_id, product_id, direction, quantity, execution_price, commission) "
            "VALUES "
            "('L_EQ', 'TRD_EQ', 'P_AAPL', 'LONG', 1, 1.0, 0), "
            "('L_CM', 'TRD_CM', 'P_CL', 'LONG', 1, 1.0, 0);");
}

/// Three CL contracts that all outlive the window, so M1..M3 never roll here; roll
/// mechanics have their own coverage in the pillar loader tests.
void SeedHistory(SQLite::Database& db, const numeraire::schedule::Date& start) {
    InsertFuturesContract(db, "CLM5", "2025-06-01");
    InsertFuturesContract(db, "CLN5", "2025-07-01");
    InsertFuturesContract(db, "CLQ5", "2025-08-01");

    numeraire::schedule::Date date = start;
    double aapl = 190.0;
    double front = 70.0;
    double second = 71.0;
    double third = 72.0;
    for (int day = 0; day < kNumSessions; ++day) {
        const std::string as_of = FormatIsoDate(date);
        InsertEquityBar(db, "AAPL", as_of, aapl);
        InsertFuturesSettle(db, "CLM5", as_of, front);
        InsertFuturesSettle(db, "CLN5", as_of, second);
        InsertFuturesSettle(db, "CLQ5", as_of, third);

        const double oil_shock = 0.011 * std::sin(0.21 * static_cast<double>(day));
        aapl *= std::exp(0.008 * std::cos(0.13 * static_cast<double>(day)));
        front *= std::exp(oil_shock);
        second *= std::exp(0.8 * oil_shock);
        third *= std::exp(0.6 * oil_shock);
        date = AddCalendarDays(date, 1);
    }
}

[[nodiscard]] HistoricalCalibratorConfig MakeConfig(const std::string& as_of, const int pillars = 3) {
    HistoricalCalibratorConfig config;
    config.as_of = ParseIsoDate(as_of);
    config.lookback_calendar_days = kNumSessions + 5;
    config.min_return_observations = 30;
    config.commodity_pillars = pillars;
    return config;
}

}  // namespace

TEST(BookFactorCalibrationTest, CommodityLegExpandsIntoPillarFactorsAlongsideEquity) {
    const fs::path path = UniqueSqlitePath();
    const auto start = ParseIsoDate("2025-01-02");
    const std::string as_of = FormatIsoDate(AddCalendarDays(start, kNumSessions - 1));
    {
        SQLite::Database db(path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db.exec(ReadSchemaFile());
        SeedMixedBook(db);
        SeedHistory(db, start);
    }

    const auto result = CalibrateBookFromDatabase(path.string(), MakeConfig(as_of), std::string_view{"BOOK_MIX"});

    // One factor for the equity, a whole strip for the single commodity leg.
    // Discovery orders by asset_kind, so COMMODITY comes before EQUITY.
    ASSERT_EQ(result.factor_ids.size(), 4U);
    EXPECT_EQ(result.factor_ids[0], "CL_M1");
    EXPECT_EQ(result.factor_ids[1], "CL_M2");
    EXPECT_EQ(result.factor_ids[2], "CL_M3");
    EXPECT_EQ(result.factor_ids[3], "AAPL");

    EXPECT_EQ(result.num_return_observations, static_cast<std::size_t>(kNumSessions - 1));
    EXPECT_EQ(result.cholesky.n, 4U);
    EXPECT_EQ(result.correlation.size(), 16U);

    // Every factor carries a usable price anchor.
    for (std::size_t i = 0; i < result.spots_as_of.size(); ++i) {
        EXPECT_GT(result.spots_as_of[i], 0.0) << "factor " << result.factor_ids[i];
        EXPECT_GT(result.volatilities[i], 0.0) << "factor " << result.factor_ids[i];
    }

    // Damped shocks down the curve reproduce the Samuelson shape.
    EXPECT_GT(result.volatilities[0], result.volatilities[1]);
    EXPECT_GT(result.volatilities[1], result.volatilities[2]);

    // Pillars move together but are not the same factor, which is exactly what makes
    // a calendar spread carry risk instead of collapsing to zero.
    const double rho_m1_m2 = result.correlation[1];
    EXPECT_GT(rho_m1_m2, 0.9);
    EXPECT_LT(rho_m1_m2, 1.0);

    fs::remove(path);
}

TEST(BookFactorCalibrationTest, PillarCountIsConfigurable) {
    const fs::path path = UniqueSqlitePath();
    const auto start = ParseIsoDate("2025-01-02");
    const std::string as_of = FormatIsoDate(AddCalendarDays(start, kNumSessions - 1));
    {
        SQLite::Database db(path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db.exec(ReadSchemaFile());
        SeedMixedBook(db);
        SeedHistory(db, start);
    }

    const auto result = CalibrateBookFromDatabase(path.string(), MakeConfig(as_of, 2), std::string_view{"BOOK_MIX"});
    ASSERT_EQ(result.factor_ids.size(), 3U);
    EXPECT_EQ(result.factor_ids[0], "CL_M1");
    EXPECT_EQ(result.factor_ids[1], "CL_M2");
    EXPECT_EQ(result.factor_ids[2], "AAPL");

    fs::remove(path);
}

TEST(BookFactorCalibrationTest, RejectsAsOfWithoutACommoditySession) {
    const fs::path path = UniqueSqlitePath();
    const auto start = ParseIsoDate("2025-01-02");
    {
        SQLite::Database db(path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db.exec(ReadSchemaFile());
        SeedMixedBook(db);
        SeedHistory(db, start);
    }

    // One day past the last seeded session: the anchor would otherwise be stale.
    const std::string as_of = FormatIsoDate(AddCalendarDays(start, kNumSessions));
    EXPECT_THROW(CalibrateBookFromDatabase(path.string(), MakeConfig(as_of), std::string_view{"BOOK_MIX"}),
                 numeraire::ValidationError);

    fs::remove(path);
}
