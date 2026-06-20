#include <gtest/gtest.h>

#include <numeraire/database/sqlite_trade_repository.hpp>
#include <numeraire/database/trade_booking_rules.hpp>
#include <numeraire/database/trade_lifecycle.hpp>

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
    fs::path const schema = fs::path(NUMERAIRE_SOURCE_DIR) / "sql" / "schema_v1.sql";
    std::ifstream in(schema);
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

void SeedLifecycleDb(const std::string& db_path) {
    SQLite::Database db(db_path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec(ReadSchemaFile());

    db.exec(
            "INSERT INTO products (product_id, asset_kind, underlying_id, expiry_date, settlement, "
            "currency, contract_size, day_count, calendar) VALUES "
            "('P_OLD', 'EQUITY', 'AAPL', '2026-06-01', 'PHYSICAL', 'USD', 100.0, 'Actual365Fixed', "
            "'UnitedStates'), "
            "('P_LIVE', 'EQUITY', 'MSFT', '2026-12-18', 'PHYSICAL', 'USD', 100.0, 'Actual365Fixed', "
            "'UnitedStates');");

    db.exec(
            "INSERT INTO products_equity (product_id, instrument_type, option_type, strike, "
            "exercise_style, structured_params) VALUES "
            "('P_OLD', 'plain_vanilla_european_option', 'call', 100, 'european', '{}'), "
            "('P_LIVE', 'plain_vanilla_european_option', 'call', 200, 'european', '{}');");

    db.exec(
            "INSERT INTO trades (trade_id, portfolio_id, strategy_type, booking_timestamp, trade_date, "
            "updated_at, status) VALUES "
            "('TRD_OLD', 'BOOK_1', 'VANILLA_OPTION', '2026-01-01 10:00:00', '2026-01-02', "
            "'2026-01-02', 'LIVE'), "
            "('TRD_LIVE', 'BOOK_1', 'VANILLA_OPTION', '2026-01-01 10:00:00', '2026-01-02', "
            "'2026-01-02', 'LIVE');");

    db.exec(
            "INSERT INTO trade_legs (leg_id, trade_id, product_id, direction, quantity, "
            "execution_price, commission) VALUES "
            "('TRD_OLD_L1', 'TRD_OLD', 'P_OLD', 'LONG', 1, 1.0, 0), "
            "('TRD_LIVE_L1', 'TRD_LIVE', 'P_LIVE', 'LONG', 1, 1.0, 0);");
}

[[nodiscard]] std::string TradeStatus(const std::string& db_path, const std::string& trade_id) {
    SQLite::Database db(db_path, SQLite::OPEN_READONLY);
    SQLite::Statement st(db, "SELECT status FROM trades WHERE trade_id = ?");
    st.bind(1, trade_id);
    EXPECT_TRUE(st.executeStep());
    return st.getColumn(0).getString();
}

}  // namespace

TEST(TradeLifecycleTest, KeepsLiveOnExpiryDayExpiresDayAfter) {
    const std::string path = TempSqlitePath();
    SeedLifecycleDb(path);

    const auto on_expiry =
            numeraire::database::ApplyTradeLifecycleAsOf(path, "2026-06-01", std::string_view{"BOOK_1"});
    EXPECT_TRUE(on_expiry.expired_trade_ids.empty());
    EXPECT_TRUE(numeraire::database::TradeStatusEquals(TradeStatus(path, "TRD_OLD"),
                                                      numeraire::database::kTradeStatusLive));

    const auto after_expiry =
            numeraire::database::ApplyTradeLifecycleAsOf(path, "2026-06-02", std::string_view{"BOOK_1"});
    ASSERT_EQ(after_expiry.expired_trade_ids.size(), 1U);
    EXPECT_EQ(after_expiry.expired_trade_ids.front(), "TRD_OLD");
    EXPECT_TRUE(numeraire::database::TradeStatusEquals(TradeStatus(path, "TRD_OLD"),
                                                      numeraire::database::kTradeStatusExpired));
    EXPECT_TRUE(numeraire::database::TradeStatusEquals(TradeStatus(path, "TRD_LIVE"),
                                                      numeraire::database::kTradeStatusLive));

    numeraire::database::SqliteTradeRepository repo(path);
    const auto live_ids = repo.ListLiveTradeIdsForPortfolio("BOOK_1");
    ASSERT_EQ(live_ids.size(), 1U);
    EXPECT_EQ(live_ids.front(), "TRD_LIVE");

    fs::remove(path);
}
