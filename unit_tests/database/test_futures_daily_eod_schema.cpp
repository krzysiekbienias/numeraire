#include <SQLiteCpp/SQLiteCpp.h>
#include <gtest/gtest.h>

#include <cstdint>
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

void InsertSampleFuturesBar(SQLite::Database& db) {
    SQLite::Statement ins(
        db,
        "INSERT INTO futures_daily_eod (ticker, product_code, as_of, session_calendar, open, high, low, "
        "close, settlement_price, currency, volume, dollar_volume, vwap, trade_count, source, timespan, "
        "provider_timestamp_utc_ms, ingested_at) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    ins.bind(1, "GCJ5");
    ins.bind(2, "GC");
    ins.bind(3, "2025-02-04");
    ins.bind(4, "America/Chicago");
    ins.bind(5, 2850.4);
    ins.bind(6, 2877.1);
    ins.bind(7, 2837.4);
    ins.bind(8, 2874.2);
    ins.bind(9, 2875.8);
    ins.bind(10, "USD");
    ins.bind(11, 133127.0);
    ins.bind(12, 380717446.0);
    ins.bind(13, 2860.0);
    ins.bind(14, 74262);
    ins.bind(15, "massive");
    ins.bind(16, "1session");
    ins.bind(17, static_cast<std::int64_t>(1738540800000));
    ins.bind(18, "2025-02-05T12:00:00Z");
    ins.exec();
}

}  // namespace

TEST(FuturesDailyEodSchemaTest, InsertAndQueryRoundTrip) {
    SQLite::Database db(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec(ReadSchemaFile());
    InsertSampleFuturesBar(db);

    SQLite::Statement q(db,
                        "SELECT as_of, close, settlement_price, product_code, session_calendar "
                        "FROM futures_daily_eod WHERE ticker = 'GCJ5'");
    ASSERT_TRUE(q.executeStep());
    EXPECT_EQ(q.getColumn(0).getString(), std::string("2025-02-04"));
    EXPECT_DOUBLE_EQ(q.getColumn(1).getDouble(), 2874.2);
    EXPECT_DOUBLE_EQ(q.getColumn(2).getDouble(), 2875.8);
    EXPECT_EQ(q.getColumn(3).getString(), std::string("GC"));
    EXPECT_EQ(q.getColumn(4).getString(), std::string("America/Chicago"));
}

TEST(FuturesDailyEodSchemaTest, UniqueConstraintRejectsDuplicateKey) {
    SQLite::Database db(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec(ReadSchemaFile());
    InsertSampleFuturesBar(db);
    EXPECT_THROW(InsertSampleFuturesBar(db), SQLite::Exception);
}
