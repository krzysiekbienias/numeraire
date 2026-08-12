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

void InsertParentProduct(SQLite::Database& db) {
    db.exec(
            "INSERT INTO products (product_id, asset_kind, underlying_id, expiry_date, settlement, "
            "currency, contract_size, day_count, calendar) "
            "VALUES ('FUT_OUTRIGHT_CL_CLU6', 'COMMODITY', 'CL', '2026-08-20', 'PHYSICAL', "
            "'USD', 1000.0, 'Actual365Fixed', 'UnitedStates')");
}

void InsertCommodityExtension(SQLite::Database& db) {
    SQLite::Statement ins(
            db,
            "INSERT INTO products_commodity ("
            "product_id, instrument_type, product_code, contract_ticker, contract_month, "
            "settlement_date, multiplier, tick_size, tick_value, option_type, strike, "
            "exercise_style, option_ticker, underlying_contract_ticker, structured_params) "
            "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    ins.bind(1, "FUT_OUTRIGHT_CL_CLU6");
    ins.bind(2, "commodity_futures_outright");
    ins.bind(3, "CL");
    ins.bind(4, "CLU6");
    ins.bind(5, "U6");
    ins.bind(6, "2026-08-20");
    ins.bind(7, 1000.0);
    ins.bind(8, 0.01);
    ins.bind(9, 10.0);
    ins.bind(10);  // option_type NULL
    ins.bind(11);
    ins.bind(12);
    ins.bind(13);
    ins.bind(14);
    ins.bind(15, "{}");
    ins.exec();
}

}  // namespace

TEST(ProductsCommoditySchemaTest, InsertAndQueryOutrightRoundTrip) {
    SQLite::Database db(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec("PRAGMA foreign_keys = ON;");
    db.exec(ReadSchemaFile());
    InsertParentProduct(db);
    InsertCommodityExtension(db);

    SQLite::Statement q(db,
                        "SELECT c.instrument_type, c.product_code, c.contract_ticker, "
                        "c.multiplier, p.asset_kind, p.underlying_id "
                        "FROM products_commodity c "
                        "JOIN products p ON p.product_id = c.product_id "
                        "WHERE c.product_id = 'FUT_OUTRIGHT_CL_CLU6'");
    ASSERT_TRUE(q.executeStep());
    EXPECT_EQ(q.getColumn(0).getString(), std::string("commodity_futures_outright"));
    EXPECT_EQ(q.getColumn(1).getString(), std::string("CL"));
    EXPECT_EQ(q.getColumn(2).getString(), std::string("CLU6"));
    EXPECT_DOUBLE_EQ(q.getColumn(3).getDouble(), 1000.0);
    EXPECT_EQ(q.getColumn(4).getString(), std::string("COMMODITY"));
    EXPECT_EQ(q.getColumn(5).getString(), std::string("CL"));
}

TEST(ProductsCommoditySchemaTest, ForeignKeyRequiresParentProduct) {
    SQLite::Database db(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec("PRAGMA foreign_keys = ON;");
    db.exec(ReadSchemaFile());
    EXPECT_THROW(InsertCommodityExtension(db), SQLite::Exception);
}

TEST(ProductsCommoditySchemaTest, CascadeDeletesExtension) {
    SQLite::Database db(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec("PRAGMA foreign_keys = ON;");
    db.exec(ReadSchemaFile());
    InsertParentProduct(db);
    InsertCommodityExtension(db);

    db.exec("DELETE FROM products WHERE product_id = 'FUT_OUTRIGHT_CL_CLU6'");
    SQLite::Statement q(db, "SELECT COUNT(*) FROM products_commodity");
    ASSERT_TRUE(q.executeStep());
    EXPECT_EQ(q.getColumn(0).getInt(), 0);
}
