#include <gtest/gtest.h>

#include <numeraire/database/underlying_daily_closes.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/schedule/format_iso_date.hpp>
#include <numeraire/simulation/historical_calibrator.hpp>

#include <SQLiteCpp/SQLiteCpp.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

using numeraire::database::DailyCloseObservation;
using numeraire::database::ListDistinctBookUnderlyingIds;
using numeraire::database::LoadUnderlyingDailyClosesRange;
using numeraire::schedule::AddCalendarDays;
using numeraire::schedule::FormatIsoDate;
using numeraire::schedule::ParseIsoDate;
using numeraire::simulation::CalibrateBookFromDatabase;
using numeraire::simulation::CalibrateFromDatabase;
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
    return fs::temp_directory_path() / ("numeraire_underlying_closes_ut_" + std::to_string(ms) + ".sqlite3");
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

void InsertIndexBar(SQLite::Database& db, const std::string& ticker, const std::string& as_of, double close) {
    SQLite::Statement ins(
            db,
            "INSERT INTO index_daily_eod (ticker, as_of, session_calendar, open, high, low, close, currency, "
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

void SeedBookWithTwoUnderlyings(SQLite::Database& db) {
    db.exec(
            "INSERT INTO products (product_id, asset_kind, underlying_id, expiry_date, settlement, currency, "
            "contract_size, day_count, calendar) VALUES "
            "('P_AAPL', 'EQUITY', 'AAPL', '2026-12-31', 'PHYSICAL', 'USD', 100.0, 'Actual365Fixed', "
            "'UnitedStates'), "
            "('P_NDX', 'EQUITY', 'NDX', '2026-12-31', 'PHYSICAL', 'USD', 100.0, 'Actual365Fixed', "
            "'UnitedStates');");
    db.exec(
            "INSERT INTO products_equity (product_id, instrument_type, option_type, strike, exercise_style, "
            "structured_params) VALUES "
            "('P_AAPL', 'plain_vanilla_european_option', 'call', 100, 'european', '{}'), "
            "('P_NDX', 'plain_vanilla_european_option', 'call', 100, 'european', '{}');");
    db.exec(
            "INSERT INTO trades (trade_id, portfolio_id, strategy_type, booking_timestamp, trade_date, updated_at, "
            "status) VALUES "
            "('TRD_A', 'BOOK_1', 'VANILLA_OPTION', '2026-01-01 10:00:00', '2026-01-02', '2026-01-02', 'LIVE'), "
            "('TRD_B', 'BOOK_1', 'VANILLA_OPTION', '2026-01-01 10:00:00', '2026-01-02', '2026-01-02', 'LIVE');");
    db.exec(
            "INSERT INTO trade_legs (leg_id, trade_id, product_id, direction, quantity, execution_price, commission) "
            "VALUES "
            "('L_A', 'TRD_A', 'P_AAPL', 'LONG', 1, 1.0, 0), "
            "('L_B', 'TRD_B', 'P_NDX', 'LONG', 1, 1.0, 0);");
}

void SeedCorrelatedHistory(SQLite::Database& db, const numeraire::schedule::Date& start, int num_days) {
    numeraire::schedule::Date date = start;
    double aapl = 100.0;
    double ndx = 20000.0;
    for (int day = 0; day < num_days; ++day) {
        const std::string as_of = FormatIsoDate(date);
        const double shock = 0.01 * std::sin(0.13 * static_cast<double>(day));
        aapl *= std::exp(shock);
        ndx *= std::exp(shock);
        InsertEquityBar(db, "AAPL", as_of, aapl);
        InsertIndexBar(db, "I:NDX", as_of, ndx);
        date = AddCalendarDays(date, 1);
    }
}

TEST(UnderlyingDailyClosesTest, LoadsEquityRangeAndBookUnderlyings) {
    const fs::path path = UniqueSqlitePath();
    const auto start = ParseIsoDate("2025-01-02");
    const int num_days = 90;
    {
        SQLite::Database db(path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db.exec(ReadSchemaFile());
        SeedBookWithTwoUnderlyings(db);
        SeedCorrelatedHistory(db, start, num_days);
    }

    const auto underlyings = ListDistinctBookUnderlyingIds(path.string());
    ASSERT_EQ(underlyings.size(), 2U);
    EXPECT_EQ(underlyings[0], "AAPL");
    EXPECT_EQ(underlyings[1], "NDX");

    const std::string from_iso = FormatIsoDate(start);
    const std::string to_iso = FormatIsoDate(AddCalendarDays(start, num_days - 1));
    const std::vector<DailyCloseObservation> aapl =
            LoadUnderlyingDailyClosesRange(path.string(), "AAPL", from_iso, to_iso, 1);
    const std::vector<DailyCloseObservation> ndx =
            LoadUnderlyingDailyClosesRange(path.string(), "NDX", from_iso, to_iso, 1);
    EXPECT_EQ(aapl.size(), static_cast<std::size_t>(num_days));
    EXPECT_EQ(ndx.size(), static_cast<std::size_t>(num_days));

    HistoricalCalibratorConfig config;
    config.as_of = ParseIsoDate(to_iso);
    config.lookback_calendar_days = num_days + 5;
    config.min_return_observations = 60;

    const auto explicit_factors = CalibrateFromDatabase(path.string(), underlyings, config);
    EXPECT_EQ(explicit_factors.factor_ids.size(), 2U);
    EXPECT_NEAR(explicit_factors.correlation[1], 1.0, 1.0e-5);
    EXPECT_NEAR(explicit_factors.correlation[2], 1.0, 1.0e-5);

    const auto book = CalibrateBookFromDatabase(path.string(), config);
    EXPECT_EQ(book.factor_ids, underlyings);
    EXPECT_GE(book.num_return_observations, 60U);

    fs::remove(path);
}

}  // namespace
