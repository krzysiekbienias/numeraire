#include <gtest/gtest.h>
#include <numeraire/database/futures_daily_eod_lookup.hpp>
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

void InsertFuturesBar(SQLite::Database& db, const std::string& ticker, const std::string& as_of,
                      double settle, double close) {
    SQLite::Statement ins(
            db,
            "INSERT INTO futures_daily_eod (ticker, product_code, as_of, session_calendar, open, high, "
            "low, close, settlement_price, currency, volume, dollar_volume, vwap, trade_count, source, "
            "timespan, provider_timestamp_utc_ms, ingested_at) "
            "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    ins.bind(1, ticker);
    ins.bind(2, "CL");
    ins.bind(3, as_of);
    ins.bind(4, "America/Chicago");
    ins.bind(5, close);
    ins.bind(6, close);
    ins.bind(7, close);
    ins.bind(8, close);
    ins.bind(9, settle);
    ins.bind(10, "USD");
    ins.bind(11, 1000.0);
    ins.bind(12);
    ins.bind(13);
    ins.bind(14);
    ins.bind(15, "ut");
    ins.bind(16, "1session");
    ins.bind(17);
    ins.bind(18, "2026-01-01T00:00:00Z");
    ins.exec();
}

[[nodiscard]] fs::path UniqueSqlitePath() {
    using namespace std::chrono;
    const auto ms = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    return fs::temp_directory_path() /
           ("numeraire_futures_eod_lookup_ut_" + std::to_string(ms) + ".sqlite3");
}

}  // namespace

TEST(FuturesDailyEodLookupTest, PrefersSettlementOverClose) {
    fs::path const path = UniqueSqlitePath();
    std::error_code unlink_ec;
    fs::remove(path, unlink_ec);
    {
        SQLite::Database db(path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db.exec(ReadSchemaFile());
        InsertFuturesBar(db, "CLX6", "2026-08-11", 80.31, 80.37);
    }

    const std::optional<double> px =
            numeraire::database::LookupFuturesDailySettlement(path.string(), "clx6", "2026-08-11");
    ASSERT_TRUE(px.has_value());
    EXPECT_DOUBLE_EQ(*px, 80.31);
    fs::remove(path);
}

TEST(FuturesDailyEodLookupTest, MissingRowReturnsNullopt) {
    fs::path const path = UniqueSqlitePath();
    std::error_code unlink_ec;
    fs::remove(path, unlink_ec);
    {
        SQLite::Database db(path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db.exec(ReadSchemaFile());
    }
    EXPECT_FALSE(
            numeraire::database::LookupFuturesDailySettlement(path.string(), "CLX6", "2026-08-11")
                    .has_value());
    fs::remove(path);
}
