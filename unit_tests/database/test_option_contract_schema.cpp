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
            "INSERT INTO option_contract (option_ticker, listing_as_of, underlying_ticker, "
            "expiration_date, strike_price, contract_type, exercise_style, shares_per_contract, "
            "primary_exchange, cfi, currency, source, ingested_at) "
            "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)");
    ins.bind(1, "O:NDX260515C05000000");
    ins.bind(2, "2026-05-15");
    ins.bind(3, "NDX");
    ins.bind(4, "2026-05-15");
    ins.bind(5, 5000.0);
    ins.bind(6, "call");
    ins.bind(7, "european");
    ins.bind(8, 100);
    ins.bind(9, "GMNI");
    ins.bind(10, "OCEICS");
    ins.bind(11, "USD");
    ins.bind(12, "polygon");
    ins.bind(13, "2026-05-16T12:00:00Z");
    ins.exec();
}

}  // namespace

TEST(OptionContractSchemaTest, InsertAndQueryRoundTrip) {
    SQLite::Database db(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec(ReadSchemaFile());
    InsertSampleContract(db);

    SQLite::Statement q(db,
                        "SELECT listing_as_of, strike_price, exercise_style FROM option_contract "
                        "WHERE option_ticker = 'O:NDX260515C05000000'");
    ASSERT_TRUE(q.executeStep());
    EXPECT_EQ(q.getColumn(0).getString(), std::string("2026-05-15"));
    EXPECT_DOUBLE_EQ(q.getColumn(1).getDouble(), 5000.0);
    EXPECT_EQ(q.getColumn(2).getString(), std::string("european"));
}

TEST(OptionContractSchemaTest, UniqueConstraintRejectsDuplicateKey) {
    SQLite::Database db(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec(ReadSchemaFile());
    InsertSampleContract(db);
    EXPECT_THROW(InsertSampleContract(db), SQLite::Exception);
}
