#include <gtest/gtest.h>

#include <numeraire/database/historical_calibration_types.hpp>
#include <numeraire/database/sqlite_historical_calibration_repository.hpp>
#include <numeraire/database/underlying_daily_closes.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/schedule/format_iso_date.hpp>
#include <numeraire/simulation/historical_calibration_eod_builder.hpp>
#include <numeraire/simulation/historical_calibrator.hpp>

#include <SQLiteCpp/SQLiteCpp.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

using numeraire::database::HistoricalCalibrationCholeskyWrite;
using numeraire::database::HistoricalCalibrationCorrelationWrite;
using numeraire::database::HistoricalCalibrationFactorWrite;
using numeraire::database::HistoricalCalibrationHeaderWrite;
using numeraire::database::ListDistinctBookUnderlyingIds;
using numeraire::database::SqliteHistoricalCalibrationRepository;
using numeraire::schedule::AddCalendarDays;
using numeraire::schedule::FormatIsoDate;
using numeraire::schedule::ParseIsoDate;
using numeraire::simulation::BuildHistoricalCalibrationEod;
using numeraire::simulation::CalibrateBookFromDatabase;
using numeraire::simulation::HistoricalCalibrationBuildParams;
using numeraire::simulation::HistoricalCalibratorConfig;

[[nodiscard]] std::string ReadSchemaFile() {
    const fs::path schema = fs::path(NUMERAIRE_SOURCE_DIR) / "sql" / "schema_v1.sql";
    std::ifstream in(schema);
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

[[nodiscard]] fs::path UniqueSqlitePath() {
    using namespace std::chrono;
    const auto ms = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    return fs::temp_directory_path() / ("numeraire_hist_cal_ut_" + std::to_string(ms) + ".sqlite3");
}

void InsertEquityBar(SQLite::Database& db, const std::string& ticker, const std::string& as_of, double close) {
    SQLite::Statement ins(
            db,
            "INSERT INTO equity_daily_eod (ticker, as_of, session_calendar, open, high, low, close, currency, "
            "volume, vwap, trade_count, source, timespan, adjusted, provider_timestamp_utc_ms, ingested_at) "
            "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    ins.bind(1, ticker);
    ins.bind(2, as_of);
    ins.bind(3, "America/New_York");
    ins.bind(4, close);
    ins.bind(5, close);
    ins.bind(6, close);
    ins.bind(7, close);
    ins.bind(8, "USD");
    ins.bind(9, nullptr);
    ins.bind(10, nullptr);
    ins.bind(11, nullptr);
    ins.bind(12, "ut");
    ins.bind(13, "1d");
    ins.bind(14, 1);
    ins.bind(15, nullptr);
    ins.bind(16, "2026-01-01T00:00:00Z");
    ins.exec();
}

void SeedTwoPortfolioBook(SQLite::Database& db) {
    db.exec(
            "INSERT INTO products (product_id, asset_kind, underlying_id, expiry_date, settlement, currency, "
            "contract_size, day_count, calendar) VALUES "
            "('P_AAPL', 'EQUITY', 'AAPL', '2026-12-31', 'PHYSICAL', 'USD', 100.0, 'Actual365Fixed', "
            "'UnitedStates'), "
            "('P_MSFT', 'EQUITY', 'MSFT', '2026-12-31', 'PHYSICAL', 'USD', 100.0, 'Actual365Fixed', "
            "'UnitedStates');");
    db.exec(
            "INSERT INTO products_equity (product_id, instrument_type, option_type, strike, exercise_style, "
            "structured_params) VALUES "
            "('P_AAPL', 'plain_vanilla_european_option', 'call', 100, 'european', '{}'), "
            "('P_MSFT', 'plain_vanilla_european_option', 'call', 100, 'european', '{}');");
    db.exec(
            "INSERT INTO trades (trade_id, portfolio_id, strategy_type, booking_timestamp, trade_date, updated_at, "
            "status) VALUES "
            "('TRD_A', 'BOOK_1', 'VANILLA_OPTION', '2026-01-01 10:00:00', '2026-01-02', '2026-01-02', 'LIVE'), "
            "('TRD_B', 'BOOK_2', 'VANILLA_OPTION', '2026-01-01 10:00:00', '2026-01-02', '2026-01-02', 'LIVE');");
    db.exec(
            "INSERT INTO trade_legs (leg_id, trade_id, product_id, direction, quantity, execution_price, commission) "
            "VALUES "
            "('L_A', 'TRD_A', 'P_AAPL', 'LONG', 1, 1.0, 0), "
            "('L_B', 'TRD_B', 'P_MSFT', 'LONG', 1, 1.0, 0);");
}

void SeedCorrelatedHistory(SQLite::Database& db, const numeraire::schedule::Date& start, int num_days) {
    numeraire::schedule::Date date = start;
    double aapl = 100.0;
    double msft = 200.0;
    for (int day = 0; day < num_days; ++day) {
        const std::string as_of = FormatIsoDate(date);
        const double shock = 0.01 * std::sin(0.13 * static_cast<double>(day));
        aapl *= std::exp(shock);
        msft *= std::exp(shock);
        InsertEquityBar(db, "AAPL", as_of, aapl);
        InsertEquityBar(db, "MSFT", as_of, msft);
        date = AddCalendarDays(date, 1);
    }
}

}  // namespace

TEST(SqliteHistoricalCalibrationRepositoryTest, UpsertReplacesOfficialKey) {
    const fs::path path = UniqueSqlitePath();
    {
        SQLite::Database db(path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db.exec(ReadSchemaFile());
    }

    HistoricalCalibrationHeaderWrite header{};
    header.scope_key = "BOOK_1";
    header.as_of = "2026-01-15";
    header.history_start = "2024-01-16";
    header.history_end = "2026-01-15";
    header.lookback_calendar_days = 504;
    header.min_return_observations = 60;
    header.num_factors = 1;
    header.num_return_observations = 120;
    header.batch_run_id = "ut-1";

    const std::vector<HistoricalCalibrationFactorWrite> factors{
            {.factor_index = 0, .underlying_id = "AAPL", .spot_as_of = 190.0, .volatility = 0.22},
    };
    const std::vector<HistoricalCalibrationCorrelationWrite> correlations{
            {.factor_i = 0, .factor_j = 0, .rho = 1.0},
    };
    const std::vector<HistoricalCalibrationCholeskyWrite> cholesky{
            {.row_i = 0, .col_j = 0, .l_value = 1.0},
    };

    SqliteHistoricalCalibrationRepository repo(path.string());
    const long first_id = repo.UpsertSnapshot(header, factors, correlations, cholesky);
    header.batch_run_id = "ut-2";
    const std::vector<HistoricalCalibrationFactorWrite> factors_v2{
            {.factor_index = 0, .underlying_id = "AAPL", .spot_as_of = 190.0, .volatility = 0.25},
    };
    const long second_id = repo.UpsertSnapshot(header, factors_v2, correlations, cholesky);
    EXPECT_NE(first_id, second_id);

    SQLite::Database db(path.string(), SQLite::OPEN_READONLY);
    SQLite::Statement count(db, "SELECT COUNT(*) FROM historical_calibration WHERE scope_key = 'BOOK_1'");
    ASSERT_TRUE(count.executeStep());
    EXPECT_EQ(count.getColumn(0).getInt(), 1);

    SQLite::Statement vol(db,
                          "SELECT volatility FROM historical_calibration_factor WHERE calibration_id = ?");
    vol.bind(1, second_id);
    ASSERT_TRUE(vol.executeStep());
    EXPECT_DOUBLE_EQ(vol.getColumn(0).getDouble(), 0.25);

    fs::remove(path);
}

TEST(HistoricalCalibrationEodBuildTest, PortfolioScopeFiltersFactorsAndPersists) {
    const fs::path path = UniqueSqlitePath();
    const auto start = ParseIsoDate("2025-01-02");
    const int num_days = 90;
    const std::string as_of = FormatIsoDate(AddCalendarDays(start, num_days - 1));
    {
        SQLite::Database db(path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db.exec(ReadSchemaFile());
        SeedTwoPortfolioBook(db);
        SeedCorrelatedHistory(db, start, num_days);
    }

    EXPECT_EQ(ListDistinctBookUnderlyingIds(path.string()).size(), 2U);
    EXPECT_EQ(ListDistinctBookUnderlyingIds(path.string(), std::string_view{"LIVE"}, std::string_view{"BOOK_1"}).size(),
              1U);
    EXPECT_EQ(
            ListDistinctBookUnderlyingIds(path.string(), std::string_view{"LIVE"}, std::string_view{"BOOK_1"})[0],
            "AAPL");

    HistoricalCalibrationBuildParams params{};
    params.database_file_path = path.string();
    params.scope_key = "BOOK_1";
    params.as_of = as_of;
    params.batch_run_id = "ut-book1";
    params.lookback_calendar_days = num_days + 5;
    params.min_return_observations = 60;

    const auto stats = BuildHistoricalCalibrationEod(params);
    EXPECT_EQ(stats.num_factors, 1);
    EXPECT_GE(stats.num_return_observations, 60U);

    SQLite::Database db(path.string(), SQLite::OPEN_READONLY);
    SQLite::Statement header(
            db,
            "SELECT calibration_id, scope_key, num_factors FROM historical_calibration WHERE scope_key = 'BOOK_1'");
    ASSERT_TRUE(header.executeStep());
    EXPECT_EQ(header.getColumn(1).getString(), std::string("BOOK_1"));
    EXPECT_EQ(header.getColumn(2).getInt(), 1);

    SQLite::Statement factors(db,
                              "SELECT underlying_id FROM historical_calibration_factor WHERE calibration_id = ?");
    factors.bind(1, header.getColumn(0).getInt64());
    ASSERT_TRUE(factors.executeStep());
    EXPECT_EQ(factors.getColumn(0).getString(), std::string("AAPL"));
    EXPECT_FALSE(factors.executeStep());

    HistoricalCalibratorConfig config;
    config.as_of = ParseIsoDate(as_of);
    config.lookback_calendar_days = num_days + 5;
    config.min_return_observations = 60;
    const auto all_book = CalibrateBookFromDatabase(path.string(), config);
    EXPECT_EQ(all_book.factor_ids.size(), 2U);

    fs::remove(path);
}
