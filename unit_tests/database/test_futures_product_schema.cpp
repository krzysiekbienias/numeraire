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

void InsertSampleProduct(SQLite::Database& db) {
    SQLite::Statement ins(
        db,
        "INSERT INTO futures_product (product_code, name, asset_class, asset_sub_class, sector, "
        "sub_sector, trading_venue, type, trade_currency_code, settlement_currency_code, "
        "settlement_method, settlement_type, price_quotation, unit_of_measure, unit_of_measure_qty, "
        "as_of, last_updated, source, ingested_at) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    ins.bind(1, "CL");
    ins.bind(2, "Crude Oil");
    ins.bind(3, "commodity");
    ins.bind(4, "energy");
    ins.bind(5, "crude_oil");
    ins.bind(6);
    ins.bind(7, "XNYM");
    ins.bind(8, "single");
    ins.bind(9, "USD");
    ins.bind(10, "USD");
    ins.bind(11, "financially_settled");
    ins.bind(12, "cash");
    ins.bind(13);
    ins.bind(14, "BBL");
    ins.bind(15, 1000.0);
    ins.bind(16, "2025-03-27");
    ins.bind(17, "2025-02-22T00:20:29-06:00");
    ins.bind(18, "massive");
    ins.bind(19, "2025-03-28T12:00:00Z");
    ins.exec();
}

}  // namespace

TEST(FuturesProductSchemaTest, InsertAndQueryRoundTrip) {
    SQLite::Database db(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec(ReadSchemaFile());
    InsertSampleProduct(db);

    SQLite::Statement q(db,
                        "SELECT asset_sub_class, sector, trading_venue, unit_of_measure_qty "
                        "FROM futures_product WHERE product_code = 'CL'");
    ASSERT_TRUE(q.executeStep());
    EXPECT_EQ(q.getColumn(0).getString(), std::string("energy"));
    EXPECT_EQ(q.getColumn(1).getString(), std::string("crude_oil"));
    EXPECT_EQ(q.getColumn(2).getString(), std::string("XNYM"));
    EXPECT_DOUBLE_EQ(q.getColumn(3).getDouble(), 1000.0);
}

TEST(FuturesProductSchemaTest, PrimaryKeyRejectsDuplicateProductCode) {
    SQLite::Database db(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec(ReadSchemaFile());
    InsertSampleProduct(db);
    EXPECT_THROW(InsertSampleProduct(db), SQLite::Exception);
}

TEST(FuturesProductSchemaTest, UniverseInstrumentHasFuturesIngestFlags) {
    SQLite::Database db(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec(ReadSchemaFile());

    SQLite::Statement q(db,
                        "SELECT name FROM pragma_table_info('universe_instrument') "
                        "WHERE name IN ('ingest_futures_product', 'ingest_futures_eod') "
                        "ORDER BY name");
    ASSERT_TRUE(q.executeStep());
    EXPECT_EQ(q.getColumn(0).getString(), std::string("ingest_futures_eod"));
    ASSERT_TRUE(q.executeStep());
    EXPECT_EQ(q.getColumn(0).getString(), std::string("ingest_futures_product"));
}
