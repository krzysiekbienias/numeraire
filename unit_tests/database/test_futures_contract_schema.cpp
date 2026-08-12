#include <SQLiteCpp/SQLiteCpp.h>
#include <gtest/gtest.h>

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

void InsertSampleContract(SQLite::Database& db) {
    SQLite::Statement ins(
        db,
        "INSERT INTO futures_contract (ticker, listing_as_of, product_code, name, active, type, "
        "trading_venue, group_code, first_trade_date, last_trade_date, settlement_date, "
        "days_to_maturity, trade_tick_size, settlement_tick_size, spread_tick_size, "
        "min_order_quantity, max_order_quantity, source, ingested_at) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    ins.bind(1, "CLH26");
    ins.bind(2, "2026-08-12");
    ins.bind(3, "CL");
    ins.bind(4, "Crude Oil Mar 2026");
    ins.bind(5, 1);
    ins.bind(6, "single");
    ins.bind(7, "XNYM");
    ins.bind(8);
    ins.bind(9, "2025-01-15");
    ins.bind(10, "2026-02-20");
    ins.bind(11, "2026-02-20");
    ins.bind(12, 192);
    ins.bind(13, 0.01);
    ins.bind(14, 0.01);
    ins.bind(15, 0.01);
    ins.bind(16, 1);
    ins.bind(17, 10000);
    ins.bind(18, "massive");
    ins.bind(19, "2026-08-12T12:00:00Z");
    ins.exec();
}

}  // namespace

TEST(FuturesContractSchemaTest, InsertAndQueryRoundTrip) {
    SQLite::Database db(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec(ReadSchemaFile());
    InsertSampleContract(db);

    SQLite::Statement q(db,
                        "SELECT product_code, listing_as_of, settlement_date, active "
                        "FROM futures_contract WHERE ticker = 'CLH26'");
    ASSERT_TRUE(q.executeStep());
    EXPECT_EQ(q.getColumn(0).getString(), std::string("CL"));
    EXPECT_EQ(q.getColumn(1).getString(), std::string("2026-08-12"));
    EXPECT_EQ(q.getColumn(2).getString(), std::string("2026-02-20"));
    EXPECT_EQ(q.getColumn(3).getInt(), 1);
}

TEST(FuturesContractSchemaTest, UniqueConstraintRejectsDuplicateKey) {
    SQLite::Database db(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec(ReadSchemaFile());
    InsertSampleContract(db);
    EXPECT_THROW(InsertSampleContract(db), SQLite::Exception);
}
