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

long InsertSurface(SQLite::Database& db) {
    SQLite::Statement ins(
            db,
            "INSERT INTO vol_surface_eod (underlying_id, as_of, surface_kind, coordinate_system, "
            "spot_used, risk_free_rate, dividend_yield, model, price_source, currency, point_count, "
            "ingested_at, batch_run_id) "
            "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)");
    ins.bind(1, "NDX");
    ins.bind(2, "2026-05-15");
    ins.bind(3, "implied_bs_eod");
    ins.bind(4, "strike_expiry");
    ins.bind(5, 29125.2);
    ins.bind(6, 0.03);
    ins.bind(7, 0.0);
    ins.bind(8, "black_scholes_european");
    ins.bind(9, "option_daily_price_eod.close");
    ins.bind(10, "USD");
    ins.bind(11, 2);
    ins.bind(12, "2026-05-16T12:00:00Z");
    ins.bind(13, "batch-test-001");
    ins.exec();
    return db.getLastInsertRowid();
}

void InsertPoint(SQLite::Database& db, const long surface_id, const char* contract_type, const double strike,
                 const double implied_vol) {
    SQLite::Statement ins(
            db,
            "INSERT INTO vol_surface_point_eod (surface_id, expiration_date, years_to_maturity, strike, "
            "contract_type, implied_vol, source_option_ticker, input_price, quality) "
            "VALUES (?,?,?,?,?,?,?,?,?)");
    ins.bind(1, surface_id);
    ins.bind(2, "2026-06-18");
    ins.bind(3, 0.12);
    ins.bind(4, strike);
    ins.bind(5, contract_type);
    ins.bind(6, implied_vol);
    ins.bind(7, "O:NDX260618C29200000");
    ins.bind(8, 762.7);
    ins.bind(9, "ok");
    ins.exec();
}

}  // namespace

TEST(VolSurfaceSchemaTest, HeaderAndPointsRoundTrip) {
    SQLite::Database db(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec(ReadSchemaFile());

    const long surface_id = InsertSurface(db);
    InsertPoint(db, surface_id, "call", 29200.0, 0.22);
    InsertPoint(db, surface_id, "put", 29200.0, 0.21);

    SQLite::Statement hdr(db, "SELECT underlying_id, point_count FROM vol_surface_eod WHERE surface_id = ?");
    hdr.bind(1, surface_id);
    ASSERT_TRUE(hdr.executeStep());
    EXPECT_EQ(hdr.getColumn(0).getString(), std::string("NDX"));
    EXPECT_EQ(hdr.getColumn(1).getInt(), 2);

    SQLite::Statement pts(db,
                          "SELECT COUNT(*), MIN(implied_vol), MAX(implied_vol) FROM vol_surface_point_eod "
                          "WHERE surface_id = ?");
    pts.bind(1, surface_id);
    ASSERT_TRUE(pts.executeStep());
    EXPECT_EQ(pts.getColumn(0).getInt(), 2);
    EXPECT_DOUBLE_EQ(pts.getColumn(1).getDouble(), 0.21);
    EXPECT_DOUBLE_EQ(pts.getColumn(2).getDouble(), 0.22);
}

TEST(VolSurfaceSchemaTest, UniqueSurfaceHeaderPerUnderlyingAsOfKind) {
    SQLite::Database db(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec(ReadSchemaFile());
    InsertSurface(db);
    EXPECT_THROW(InsertSurface(db), SQLite::Exception);
}

TEST(VolSurfaceSchemaTest, UniquePointPerStrikeExpiryAndContractType) {
    SQLite::Database db(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec(ReadSchemaFile());
    const long surface_id = InsertSurface(db);
    InsertPoint(db, surface_id, "call", 29200.0, 0.22);
    EXPECT_THROW(InsertPoint(db, surface_id, "call", 29200.0, 0.23), SQLite::Exception);
}

TEST(VolSurfaceSchemaTest, CascadeDeleteRemovesPoints) {
    SQLite::Database db(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec(ReadSchemaFile());
    const long surface_id = InsertSurface(db);
    InsertPoint(db, surface_id, "call", 29200.0, 0.22);

    SQLite::Statement del(db, "DELETE FROM vol_surface_eod WHERE surface_id = ?");
    del.bind(1, surface_id);
    del.exec();

    SQLite::Statement cnt(db, "SELECT COUNT(*) FROM vol_surface_point_eod");
    ASSERT_TRUE(cnt.executeStep());
    EXPECT_EQ(cnt.getColumn(0).getInt(), 0);
}
