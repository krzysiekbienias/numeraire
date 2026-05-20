#include <gtest/gtest.h>

#include <numeraire/database/sqlite_trade_leg_booking_repository.hpp>
#include <numeraire/utils/exception.hpp>

#include <SQLiteCpp/SQLiteCpp.h>

#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#include <filesystem>

namespace fs = std::filesystem;

namespace {

[[nodiscard]] std::string TempSqlitePath() {
    fs::path const tpl = fs::temp_directory_path() / "numeraire_booking_ut_XXXXXX";
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
    fs::path const schema = fs::path(NUMERAIRE_SOURCE_DIR) / "sql" / "schema_v1.sql";
    std::ifstream in(schema);
    if (!in) {
        throw std::runtime_error("failed to open schema: " + schema.string());
    }
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

void SeedBookingFixtureDb(const std::string& db_path) {
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
            "('TRD_001', 'BOOK_1', 'VANILLA_OPTION', NULL, '2025-08-06', "
            "'2026-05-11 16:34:30', 'LIVE');");

    db.exec(
            "INSERT INTO trade_legs (leg_id, trade_id, product_id, direction, quantity, "
            "execution_price, commission) VALUES "
            "('TRD_001_L1', 'TRD_001', 'P_AAPL_001', 'LONG', 100, 0, 75);");
}

[[nodiscard]] double ReadExecutionPrice(const std::string& db_path, const std::string& leg_id) {
    SQLite::Database db(db_path, SQLite::OPEN_READONLY);
    SQLite::Statement q(db, "SELECT execution_price FROM trade_legs WHERE leg_id = ?");
    q.bind(1, leg_id);
    if (!q.executeStep()) {
        throw std::runtime_error("leg not found: " + leg_id);
    }
    return q.getColumn(0).getDouble();
}

[[nodiscard]] std::string ReadBookingTimestamp(const std::string& db_path, const std::string& trade_id) {
    SQLite::Database db(db_path, SQLite::OPEN_READONLY);
    SQLite::Statement q(db, "SELECT booking_timestamp FROM trades WHERE trade_id = ?");
    q.bind(1, trade_id);
    if (!q.executeStep()) {
        throw std::runtime_error("trade not found: " + trade_id);
    }
    if (q.getColumn(0).isNull()) {
        return {};
    }
    return q.getColumn(0).getText();
}

}  // namespace

TEST(SqliteTradeLegBookingRepositoryTest, UpdateExecutionPrice) {
    std::string const path = TempSqlitePath();
    SeedBookingFixtureDb(path);

    numeraire::database::SqliteTradeLegBookingRepository repo(path);
    repo.UpdateExecutionPrice("TRD_001_L1", 12.5);

    EXPECT_DOUBLE_EQ(ReadExecutionPrice(path, "TRD_001_L1"), 12.5);

    fs::remove(path);
}

TEST(SqliteTradeLegBookingRepositoryTest, MissingLegThrows) {
    std::string const path = TempSqlitePath();
    SeedBookingFixtureDb(path);

    numeraire::database::SqliteTradeLegBookingRepository repo(path);
    EXPECT_THROW(repo.UpdateExecutionPrice("NO_SUCH_LEG", 1.0), numeraire::PersistenceError);

    fs::remove(path);
}

TEST(SqliteTradeLegBookingRepositoryTest, NegativeExecutionPriceThrows) {
    std::string const path = TempSqlitePath();
    SeedBookingFixtureDb(path);

    numeraire::database::SqliteTradeLegBookingRepository repo(path);
    EXPECT_THROW(repo.UpdateExecutionPrice("TRD_001_L1", -0.01), numeraire::ValidationError);

    fs::remove(path);
}

TEST(SqliteTradeLegBookingRepositoryTest, SetTradeBookingTimestampExplicit) {
    std::string const path = TempSqlitePath();
    SeedBookingFixtureDb(path);

    numeraire::database::SqliteTradeLegBookingRepository repo(path);
    repo.SetTradeBookingTimestamp("TRD_001", std::optional<std::string>{"2025-08-06 09:30:00"});

    EXPECT_EQ(ReadBookingTimestamp(path, "TRD_001"), "2025-08-06 09:30:00");

    fs::remove(path);
}

TEST(SqliteTradeLegBookingRepositoryTest, SetTradeBookingTimestampNow) {
    std::string const path = TempSqlitePath();
    SeedBookingFixtureDb(path);

    numeraire::database::SqliteTradeLegBookingRepository repo(path);
    repo.SetTradeBookingTimestamp("TRD_001", std::nullopt);

    const std::string ts = ReadBookingTimestamp(path, "TRD_001");
    EXPECT_FALSE(ts.empty());

    fs::remove(path);
}

TEST(SqliteTradeLegBookingRepositoryTest, ApplyTradeBookingTransaction) {
    std::string const path = TempSqlitePath();
    SeedBookingFixtureDb(path);

    const std::vector<numeraire::database::TradeLegBookingUpdate> updates{
            {.leg_id = "TRD_001_L1", .execution_price = 18.25},
    };

    numeraire::database::SqliteTradeLegBookingRepository repo(path);
    repo.ApplyTradeBooking("TRD_001", updates, std::optional<std::string>{"2025-08-06 10:00:00"});

    EXPECT_DOUBLE_EQ(ReadExecutionPrice(path, "TRD_001_L1"), 18.25);
    EXPECT_EQ(ReadBookingTimestamp(path, "TRD_001"), "2025-08-06 10:00:00");

    fs::remove(path);
}

TEST(SqliteTradeLegBookingRepositoryTest, ApplyTradeBookingWrongTradeRollsBack) {
    std::string const path = TempSqlitePath();
    SeedBookingFixtureDb(path);

    const std::vector<numeraire::database::TradeLegBookingUpdate> updates{
            {.leg_id = "TRD_001_L1", .execution_price = 9.0},
    };

    numeraire::database::SqliteTradeLegBookingRepository repo(path);
    EXPECT_THROW(repo.ApplyTradeBooking("WRONG_TRADE", updates, std::nullopt), numeraire::PersistenceError);

    EXPECT_DOUBLE_EQ(ReadExecutionPrice(path, "TRD_001_L1"), 0.0);
    EXPECT_TRUE(ReadBookingTimestamp(path, "TRD_001").empty());

    fs::remove(path);
}

TEST(SqliteTradeLegBookingRepositoryTest, CommissionUnchanged) {
    std::string const path = TempSqlitePath();
    SeedBookingFixtureDb(path);

    numeraire::database::SqliteTradeLegBookingRepository repo(path);
    repo.UpdateExecutionPrice("TRD_001_L1", 3.5);

    SQLite::Database db(path, SQLite::OPEN_READONLY);
    SQLite::Statement q(db, "SELECT commission FROM trade_legs WHERE leg_id = ?");
    q.bind(1, "TRD_001_L1");
    ASSERT_TRUE(q.executeStep());
    EXPECT_DOUBLE_EQ(q.getColumn(0).getDouble(), 75.0);

    fs::remove(path);
}
