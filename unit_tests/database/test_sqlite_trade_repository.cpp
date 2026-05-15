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
            "INSERT INTO products (product_id, asset_kind, underlying_id, expiry_date, settlement, "
            "currency, contract_size, day_count, calendar) VALUES "
            "('P_AAPL_001', 'EQUITY', 'AAPL', '2025-11-04', 'PHYSICAL', "
            "'USD', 100.0, 'Actual365Fixed', 'UnitedStates');");

    db.exec(
            "INSERT INTO products_equity (product_id, instrument_type, option_type, strike, "
            "exercise_style, structured_params) VALUES "
            "('P_AAPL_001', 'plain_vanilla_european_option', 'call', 233, 'european', '{}');");

    db.exec(
            "INSERT INTO trades (trade_id, portfolio_id, strategy_type, booking_timestamp, trade_date, "
            "updated_at, status) VALUES "
            "('TRD_001', 'BOOK_1', 'VANILLA_OPTION', '2025-08-04 10:00:00', '2025-08-06', "
            "'2026-05-11 16:34:30', 'LIVE');");

    db.exec(
            "INSERT INTO trade_legs (leg_id, trade_id, product_id, direction, quantity, "
            "execution_price, commission) VALUES "
            "('TRD_001_L1', 'TRD_001', 'P_AAPL_001', 'LONG', 100, 12.5, 0);");
}

}  // namespace

TEST(SqliteTradeRepositoryTest, LoadsCatalogAndBuildsProduct) {
    std::string const path = TempSqlitePath();
    SeedFixtureDb(path);

    numeraire::database::SqliteTradeRepository repo(path);
    const auto bundle = repo.GetCatalogForTrade("TRD_001");

    EXPECT_EQ(bundle.trade.trade_id, "TRD_001");
    EXPECT_EQ(bundle.trade.portfolio_id, "BOOK_1");
    ASSERT_EQ(bundle.legs.size(), 1U);
    EXPECT_EQ(bundle.legs[0].leg.product_id, "P_AAPL_001");
    EXPECT_EQ(bundle.legs[0].product.product_id, "P_AAPL_001");
    ASSERT_TRUE(bundle.legs[0].product.option_side.has_value());
    EXPECT_EQ(*bundle.legs[0].product.option_side, "call");
    ASSERT_TRUE(bundle.legs[0].product.strike.has_value());
    EXPECT_DOUBLE_EQ(*bundle.legs[0].product.strike, 233.0);
    EXPECT_EQ(bundle.legs[0].equity.underlying_id, "AAPL");
    ASSERT_TRUE(bundle.legs[0].equity.expiry_date.has_value());
    EXPECT_EQ(*bundle.legs[0].equity.expiry_date, "2025-11-04");
    ASSERT_TRUE(bundle.legs[0].equity.settlement.has_value());
    ASSERT_TRUE(bundle.legs[0].equity.day_count.has_value());
    ASSERT_TRUE(bundle.legs[0].equity.calendar.has_value());

    const auto instrument = numeraire::products::ProductFactory::MakeFromEquityCatalog(
            bundle.legs[0].product, bundle.legs[0].equity, &bundle.trade);
    ASSERT_NE(instrument, nullptr);
    EXPECT_EQ(instrument->UnderlyingId(), "AAPL");

    fs::remove(path);
}

TEST(SqliteTradeRepositoryTest, ListAllTradeIdsOrdered) {
    std::string const path = TempSqlitePath();
    SeedFixtureDb(path);

    numeraire::database::SqliteTradeRepository repo(path);
    const auto ids = repo.ListAllTradeIds();
    ASSERT_EQ(ids.size(), 1U);
    EXPECT_EQ(ids[0], "TRD_001");

    fs::remove(path);
}

TEST(SqliteTradeRepositoryTest, MissingTradeThrows) {
    std::string const path = TempSqlitePath();
    SeedFixtureDb(path);

    numeraire::database::SqliteTradeRepository repo(path);
    EXPECT_THROW({ (void)repo.GetCatalogForTrade("NO_SUCH"); }, numeraire::PersistenceError);

    fs::remove(path);
}
