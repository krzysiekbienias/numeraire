#include <numeraire/database/vol_surface_eod_builder.hpp>
#include <numeraire/quant/black_scholes_vanilla.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/utils/logger.hpp>
#include <gtest/gtest.h>

#include <SQLiteCpp/SQLiteCpp.h>
#include <cmath>
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

void SeedIndexAndOption(SQLite::Database& db) {
    SQLite::Statement idx(
            db,
            "INSERT INTO index_daily_eod (ticker, as_of, session_calendar, open, high, low, close, currency, "
            "source, timespan, adjusted, ingested_at) VALUES (?,?,?,?,?,?,?,?,?,?,?,?)");
    idx.bind(1, "I:NDX");
    idx.bind(2, "2026-05-15");
    idx.bind(3, "America/New_York");
    idx.bind(4, 29000.0);
    idx.bind(5, 29200.0);
    idx.bind(6, 28900.0);
    idx.bind(7, 29125.2);
    idx.bind(8, "USD");
    idx.bind(9, "test");
    idx.bind(10, "1d");
    idx.bind(11, 1);
    idx.bind(12, "2026-05-16T00:00:00Z");
    idx.exec();

    SQLite::Statement cat(
            db,
            "INSERT INTO option_contract (option_ticker, listing_as_of, underlying_ticker, expiration_date, "
            "strike_price, contract_type, exercise_style, shares_per_contract, source, ingested_at) "
            "VALUES (?,?,?,?,?,?,?,?,?,?)");
    cat.bind(1, "O:NDX260618C29200000");
    cat.bind(2, "2026-05-01");
    cat.bind(3, "NDX");
    cat.bind(4, "2026-06-18");
    cat.bind(5, 29200.0);
    cat.bind(6, "call");
    cat.bind(7, "european");
    cat.bind(8, 100);
    cat.bind(9, "test");
    cat.bind(10, "2026-05-16T00:00:00Z");
    cat.exec();

    const double tau = numeraire::schedule::Act365FixedYearFraction(
            numeraire::schedule::ParseIsoDate("2026-05-15"), numeraire::schedule::ParseIsoDate("2026-06-18"));
    const double market_price = numeraire::quant::EuropeanVanillaPrice(
            numeraire::OptionType::kCall, 29125.2, 29200.0, 0.03, 0.0, 0.22, tau);

    SQLite::Statement bar(
            db,
            "INSERT INTO option_daily_price_eod (option_ticker, as_of, session_calendar, open, high, low, close, "
            "currency, source, timespan, adjusted, ingested_at) VALUES (?,?,?,?,?,?,?,?,?,?,?,?)");
    bar.bind(1, "O:NDX260618C29200000");
    bar.bind(2, "2026-05-15");
    bar.bind(3, "America/New_York");
    bar.bind(4, market_price);
    bar.bind(5, market_price);
    bar.bind(6, market_price);
    bar.bind(7, market_price);
    bar.bind(8, "USD");
    bar.bind(9, "test");
    bar.bind(10, "1d");
    bar.bind(11, 1);
    bar.bind(12, "2026-05-16T00:00:00Z");
    bar.exec();
}

}  // namespace

TEST(VolSurfaceEodBuilderTest, BuildsSurfaceFromOptionBars) {
    numeraire::utils::Logger::Init();
    const fs::path db_path = fs::temp_directory_path() / "numeraire_vol_surface_builder_test.sqlite3";
    std::error_code ec;
    fs::remove(db_path, ec);

    SQLite::Database db(db_path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec(ReadSchemaFile());
    SeedIndexAndOption(db);

    numeraire::database::VolSurfaceBuildParams params{};
    params.database_file_path = db_path.string();
    params.underlying_id = "NDX";
    params.index_ticker = "I:NDX";
    params.as_of = "2026-05-15";
    params.risk_free_rate = 0.03;
    params.dividend_yield = 0.0;
    params.batch_run_id = "unit-test";

    const auto stats = numeraire::database::BuildVolSurfaceEod(params);
    EXPECT_EQ(stats.quotes_loaded, 1);
    EXPECT_EQ(stats.points_written, 1);

    SQLite::Database ro(db_path.string(), SQLite::OPEN_READONLY);
    SQLite::Statement q(ro,
                        "SELECT COUNT(*), MAX(p.implied_vol) FROM vol_surface_point_eod p "
                        "INNER JOIN vol_surface_eod h ON h.surface_id = p.surface_id "
                        "WHERE h.underlying_id = 'NDX' AND h.as_of = '2026-05-15'");
    ASSERT_TRUE(q.executeStep());
    EXPECT_EQ(q.getColumn(0).getInt(), 1);
    EXPECT_NEAR(q.getColumn(1).getDouble(), 0.22, 1.0e-5);

    fs::remove(db_path, ec);
}

TEST(VolSurfaceEodBuilderTest, UsesCurveZeroRatePerExpiry) {
    numeraire::utils::Logger::Init();
    const fs::path db_path = fs::temp_directory_path() / "numeraire_vol_surface_curve_rate_test.sqlite3";
    std::error_code ec;
    fs::remove(db_path, ec);

    SQLite::Database db(db_path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec(ReadSchemaFile());
    SeedIndexAndOption(db);

    // Flat 5% zero curve — IV inversion must use r(τ)=0.05, not flat fallback 0.03.
    constexpr double kCurveZero = 0.05;
    db.exec("PRAGMA foreign_keys = OFF;");
    db.exec(
            "INSERT INTO discount_curve_eod (curve_id, as_of, source_par_curve_id, source_par_as_of, currency, "
            "day_count, interpolation_method, bootstrap_engine, ingested_at, batch_run_id) VALUES "
            "('USD_TREASURY_PAR_FRED', '2026-05-15', 'USD_TREASURY_PAR_FRED', '2026-05-15', 'USD', "
            "'Actual365Fixed', 'linear_zero_rate', 'test', '2026-05-16T00:00:00Z', 'ut');");
    for (const double t : {0.25, 0.5, 1.0, 2.0}) {
        const double df = std::exp(-kCurveZero * t);
        SQLite::Statement pt(
                db,
                "INSERT INTO discount_curve_point_eod (curve_id, as_of, tenor, time_years, zero_rate, "
                "discount_factor) VALUES (?,?,?,?,?,?)");
        pt.bind(1, "USD_TREASURY_PAR_FRED");
        pt.bind(2, "2026-05-15");
        pt.bind(3, std::to_string(t) + "Y");
        pt.bind(4, t);
        pt.bind(5, kCurveZero);
        pt.bind(6, df);
        pt.exec();
    }
    db.exec("PRAGMA foreign_keys = ON;");

    // Reprice the seeded option with r=5% so inverted IV stays ~0.22.
    const double tau = numeraire::schedule::Act365FixedYearFraction(
            numeraire::schedule::ParseIsoDate("2026-05-15"), numeraire::schedule::ParseIsoDate("2026-06-18"));
    const double market_price = numeraire::quant::EuropeanVanillaPrice(
            numeraire::OptionType::kCall, 29125.2, 29200.0, kCurveZero, 0.0, 0.22, tau);
    {
        SQLite::Statement upd(db, "UPDATE option_daily_price_eod SET open=?, high=?, low=?, close=? "
                                  "WHERE option_ticker=? AND as_of=?");
        upd.bind(1, market_price);
        upd.bind(2, market_price);
        upd.bind(3, market_price);
        upd.bind(4, market_price);
        upd.bind(5, "O:NDX260618C29200000");
        upd.bind(6, "2026-05-15");
        upd.exec();
    }

    numeraire::database::VolSurfaceBuildParams params{};
    params.database_file_path = db_path.string();
    params.underlying_id = "NDX";
    params.index_ticker = "I:NDX";
    params.as_of = "2026-05-15";
    params.risk_free_rate = 0.03;  // must NOT be used when curve exists
    params.dividend_yield = 0.0;
    params.discount_curve_id = "USD_TREASURY_PAR_FRED";
    params.batch_run_id = "unit-test-curve";

    const auto stats = numeraire::database::BuildVolSurfaceEod(params);
    EXPECT_EQ(stats.points_written, 1);

    SQLite::Database ro(db_path.string(), SQLite::OPEN_READONLY);
    SQLite::Statement q(ro,
                        "SELECT h.risk_free_rate, p.implied_vol FROM vol_surface_eod h "
                        "JOIN vol_surface_point_eod p ON p.surface_id = h.surface_id "
                        "WHERE h.underlying_id = 'NDX' AND h.as_of = '2026-05-15'");
    ASSERT_TRUE(q.executeStep());
    EXPECT_NEAR(q.getColumn(0).getDouble(), kCurveZero, 1.0e-8);  // header = 1Y zero
    EXPECT_NEAR(q.getColumn(1).getDouble(), 0.22, 1.0e-4);

    fs::remove(db_path, ec);
}
