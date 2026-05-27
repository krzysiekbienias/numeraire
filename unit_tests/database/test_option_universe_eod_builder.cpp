#include <SQLiteCpp/SQLiteCpp.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <numeraire/database/option_universe_eod_builder.hpp>
#include <numeraire/database/option_universe_grid_config.hpp>
#include <numeraire/utils/logger.hpp>
#include <sstream>

namespace fs = std::filesystem;

namespace {

[[nodiscard]] std::string ReadSchemaFile() {
    const fs::path schema = fs::path(NUMERAIRE_SOURCE_DIR) / "sql" / "schema_v1.sql";
    std::ifstream in(schema);
    if (!in) {
        throw std::runtime_error("failed to open schema: " + schema.string());
    }
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

void SeedCatalog(SQLite::Database& db) {
    SQLite::Statement idx(
            db,
            "INSERT INTO index_daily_eod (ticker, as_of, session_calendar, open, high, low, close, currency, "
            "source, timespan, adjusted, ingested_at) VALUES (?,?,?,?,?,?,?,?,?,?,?,?)");
    idx.bind(1, "I:NDX");
    idx.bind(2, "2026-05-15");
    idx.bind(3, "America/New_York");
    idx.bind(4, 28000.0);
    idx.bind(5, 28100.0);
    idx.bind(6, 27900.0);
    idx.bind(7, 28000.0);
    idx.bind(8, "USD");
    idx.bind(9, "test");
    idx.bind(10, "1d");
    idx.bind(11, 1);
    idx.bind(12, "2026-05-16T00:00:00Z");
    idx.exec();

    auto insert_contract = [&](const char* ticker, const char* expiry, double strike, const char* type) {
        SQLite::Statement cat(
                db,
                "INSERT INTO option_contract (option_ticker, listing_as_of, underlying_ticker, expiration_date, "
                "strike_price, contract_type, exercise_style, shares_per_contract, source, ingested_at) "
                "VALUES (?,?,?,?,?,?,?,?,?,?)");
        cat.bind(1, ticker);
        cat.bind(2, "2026-05-15");
        cat.bind(3, "NDX");
        cat.bind(4, expiry);
        cat.bind(5, strike);
        cat.bind(6, type);
        cat.bind(7, "european");
        cat.bind(8, 100);
        cat.bind(9, "test");
        cat.bind(10, "2026-05-16T00:00:00Z");
        cat.exec();
    };

    insert_contract("O:NDX260522C28000000", "2026-05-22", 28000.0, "call");
    insert_contract("O:NDX260522P28000000", "2026-05-22", 28000.0, "put");
    insert_contract("O:NDX260522C29400000", "2026-05-22", 29400.0, "call");
    insert_contract("O:NDX260522P26600000", "2026-05-22", 26600.0, "put");
    insert_contract("O:NDX260618C28000000", "2026-06-18", 28000.0, "call");
    insert_contract("O:NDX260618P25200000", "2026-06-18", 25200.0, "put");
}

}  // namespace

TEST(OptionUniverseEodBuilderTest, BuildsRowsFromGrid) {
    numeraire::utils::Logger::Init();
    const fs::path db_path = fs::temp_directory_path() / "numeraire_option_universe_test.sqlite3";
    std::error_code ec;
    fs::remove(db_path, ec);

    SQLite::Database db(db_path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec(ReadSchemaFile());
    SeedCatalog(db);

    const fs::path grid_path = fs::path(NUMERAIRE_SOURCE_DIR) / "configs" / "option_universe_grid.json";
    const auto grid = numeraire::database::LoadOptionUniverseGridConfig(grid_path);

    numeraire::database::OptionUniverseBuildParams params{};
    params.database_file_path = db_path.string();
    params.listing_as_of = "2026-05-15";
    params.underlying_ticker = "NDX";
    params.index_ticker = "I:NDX";
    params.grid_config_path = grid_path;

    const auto stats = numeraire::database::BuildOptionUniverseEod(params, grid);
    EXPECT_GT(stats.rows_upserted, 0);
    EXPECT_GT(stats.points_matched, 0);

    SQLite::Statement cnt(db, "SELECT COUNT(*) FROM option_universe_eod WHERE listing_as_of = '2026-05-15'");
    ASSERT_TRUE(cnt.executeStep());
    EXPECT_EQ(cnt.getColumn(0).getInt(), stats.rows_upserted);
}
