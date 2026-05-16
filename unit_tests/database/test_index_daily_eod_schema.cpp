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

void InsertSampleIndexBar(SQLite::Database& db) {
    SQLite::Statement ins(db,
                          "INSERT INTO index_daily_eod (ticker, as_of, session_calendar, open, high, low, "
                          "close, currency, volume, vwap, trade_count, source, timespan, adjusted, "
                          "provider_timestamp_utc_ms, ingested_at) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    ins.bind(1, "I:SPX");
    ins.bind(2, "2026-05-15");
    ins.bind(3, "America/New_York");
    ins.bind(4, 5800.1);
    ins.bind(5, 5820.0);
    ins.bind(6, 5790.0);
    ins.bind(7, 5810.5);
    ins.bind(8, "USD");
    ins.bind(9);
    ins.bind(10);
    ins.bind(11);
    ins.bind(12, "massive");
    ins.bind(13, "1d");
    ins.bind(14, 1);
    ins.bind(15, static_cast<std::int64_t>(1778817600000));
    ins.bind(16, "2026-05-16T12:00:00Z");
    ins.exec();
}

}  // namespace

TEST(IndexDailyEodSchemaTest, InsertAndQueryRoundTrip) {
    SQLite::Database db(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec(ReadSchemaFile());
    InsertSampleIndexBar(db);

    SQLite::Statement q(db,
                        "SELECT as_of, close, session_calendar, provider_timestamp_utc_ms FROM index_daily_eod "
                        "WHERE ticker = 'I:SPX'");
    ASSERT_TRUE(q.executeStep());
    EXPECT_EQ(q.getColumn(0).getString(), std::string("2026-05-15"));
    EXPECT_DOUBLE_EQ(q.getColumn(1).getDouble(), 5810.5);
    EXPECT_EQ(q.getColumn(2).getString(), std::string("America/New_York"));
    EXPECT_EQ(q.getColumn(3).getInt64(), 1778817600000LL);
}

TEST(IndexDailyEodSchemaTest, UniqueConstraintRejectsDuplicateKey) {
    SQLite::Database db(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec(ReadSchemaFile());
    InsertSampleIndexBar(db);
    EXPECT_THROW(InsertSampleIndexBar(db), SQLite::Exception);
}
