#include <gtest/gtest.h>

#include <numeraire/database/sqlite_trade_repository.hpp>
#include <numeraire/products/product_factory.hpp>
#include <numeraire/utils/exception.hpp>

#include <SQLiteCpp/SQLiteCpp.h>

#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace {

[[nodiscard]] std::string TempSqlitePath() {
    fs::path const tpl = fs::temp_directory_path() / "numeraire_ut_XXXXXX";
    std::string p = tpl.string();
    std::vector<char> buf(p.begin(), p.end());
    buf.push_back('\0');
    const int fd = mkstemp(buf.data());
    if (fd < 0) {
        throw std::runtime_error("mkstemp failed for temp sqlite path");
    }
    close(fd);
    return std::string(buf.data());
}

[[nodiscard]] std::string ReadSchemaFile() {
    fs::path const schema =
            fs::path(NUMERAIRE_SOURCE_DIR) / "sql" / "schema_v1.sql";
    std::ifstream in(schema);
    if (!in) {
        throw std::runtime_error("failed to open schema: " + schema.string());
    }
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

void SeedFixtureDb(const std::string& db_path) {
    SQLite::Database db(db_path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec(ReadSchemaFile());

    db.exec(
            "INSERT INTO products VALUES "
            "('P_AAPL_001', 'EQUITY', 'AAPL', '2025-11-04', 'PHYSICAL', "
            "'Actual365Fixed', 'UnitedStates');");

    db.exec(
            "INSERT INTO products_equity VALUES "
            "('P_AAPL_001', 'CALL', 233, '{}');");

    db.exec(
            "INSERT INTO trades VALUES "
            "('TRD_001', 'P_AAPL_001', '2025-08-04 10:00:00', '2025-08-06', "
            "'2026-05-11 16:34:30', 'LIVE', 'LONG', 100, NULL);");
}

}  // namespace

TEST(SqliteTradeRepositoryTest, LoadsCatalogAndBuildsProduct) {
    std::string const path = TempSqlitePath();
    SeedFixtureDb(path);

    numeraire::database::SqliteTradeRepository repo(path);
    const auto bundle = repo.GetCatalogForTrade("TRD_001");

    EXPECT_EQ(bundle.trade.trade_id, "TRD_001");
    EXPECT_EQ(bundle.trade.product_id, "P_AAPL_001");
    EXPECT_EQ(bundle.product.product_id, "P_AAPL_001");
    ASSERT_TRUE(bundle.product.option_side.has_value());
    EXPECT_EQ(*bundle.product.option_side, "CALL");
    ASSERT_TRUE(bundle.product.strike.has_value());
    EXPECT_DOUBLE_EQ(*bundle.product.strike, 233.0);
    EXPECT_EQ(bundle.equity.underlying_id, "AAPL");
    ASSERT_TRUE(bundle.equity.settlement.has_value());
    ASSERT_TRUE(bundle.equity.day_count.has_value());
    ASSERT_TRUE(bundle.equity.calendar.has_value());

    const auto instrument = numeraire::products::ProductFactory::MakeFromEquityCatalog(
            bundle.product, bundle.equity, &bundle.trade);
    ASSERT_NE(instrument, nullptr);
    EXPECT_EQ(instrument->UnderlyingId(), "AAPL");

    fs::remove(path);
}

TEST(SqliteTradeRepositoryTest, MissingTradeThrows) {
    std::string const path = TempSqlitePath();
    SeedFixtureDb(path);

    numeraire::database::SqliteTradeRepository repo(path);
    EXPECT_THROW({ (void)repo.GetCatalogForTrade("NO_SUCH"); }, numeraire::PersistenceError);

    fs::remove(path);
}
