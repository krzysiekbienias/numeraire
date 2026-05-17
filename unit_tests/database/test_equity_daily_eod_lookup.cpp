#include <gtest/gtest.h>
#include <numeraire/database/equity_daily_eod_lookup.hpp>
#include <SQLiteCpp/SQLiteCpp.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

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

void InsertBar(SQLite::Database& db,
               const std::string& ticker,
               const std::string& as_of,
               double close,
               int adjusted) {
    SQLite::Statement ins(
            db,
            "INSERT INTO equity_daily_eod (ticker, as_of, session_calendar, open, high, low, "
            "close, currency, volume, vwap, trade_count, source, timespan, adjusted, "
            "provider_timestamp_utc_ms, ingested_at) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
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
    ins.bind(14, adjusted);
    ins.bind(15, nullptr);
    ins.bind(16, "2026-01-01T00:00:00Z");
    ins.exec();
}

[[nodiscard]] fs::path UniqueSqlitePath() {
    using namespace std::chrono;
    const auto ms =
            duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    return fs::temp_directory_path() /
           ("numeraire_equity_eod_lookup_ut_" + std::to_string(ms) + ".sqlite3");
}

}  // namespace

TEST(EquityDailyEodLookupTest, FindsCloseCaseInsensitiveTicker) {
    fs::path const path = UniqueSqlitePath();
    std::error_code unlink_ec;
    fs::remove(path, unlink_ec);
    {
        SQLite::Database db(path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db.exec(ReadSchemaFile());
        InsertBar(db, "AAPL", "2026-05-15", 300.25, 1);
    }

    const std::optional<double> c =
            numeraire::database::LookupEquityDailyClose(path.string(), "aapl", "2026-05-15", 1);
    ASSERT_TRUE(c.has_value());
    EXPECT_DOUBLE_EQ(*c, 300.25);

    fs::remove(path);
}

TEST(EquityDailyEodLookupTest, MissingRowReturnsNullopt) {
    fs::path const path = UniqueSqlitePath();
    std::error_code unlink_ec;
    fs::remove(path, unlink_ec);
    {
        SQLite::Database db(path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db.exec(ReadSchemaFile());
        InsertBar(db, "AAPL", "2026-05-15", 1.0, 1);
    }

    const std::optional<double> c =
            numeraire::database::LookupEquityDailyClose(path.string(), "AAPL", "2026-05-16", 1);
    EXPECT_FALSE(c.has_value());

    fs::remove(path);
}

TEST(EquityDailyEodLookupTest, RespectsAdjustedFlag) {
    fs::path const path = UniqueSqlitePath();
    std::error_code unlink_ec;
    fs::remove(path, unlink_ec);
    {
        SQLite::Database db(path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db.exec(ReadSchemaFile());
        InsertBar(db, "MSFT", "2026-05-15", 100.0, 0);
    }

    EXPECT_FALSE(numeraire::database::LookupEquityDailyClose(path.string(), "MSFT", "2026-05-15", 1).has_value());
    const auto c = numeraire::database::LookupEquityDailyClose(path.string(), "MSFT", "2026-05-15", 0);
    ASSERT_TRUE(c.has_value());
    EXPECT_DOUBLE_EQ(*c, 100.0);

    fs::remove(path);
}
