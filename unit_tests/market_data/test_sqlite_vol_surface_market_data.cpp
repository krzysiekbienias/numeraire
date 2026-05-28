#include <SQLiteCpp/SQLiteCpp.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <numeraire/database/sqlite_vol_surface_repository.hpp>
#include <numeraire/database/vol_surface_types.hpp>
#include <numeraire/market_data/sqlite_vol_surface_market_data.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/utils/exception.hpp>
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

void SeedSurface(const fs::path& db_path) {
    numeraire::database::SqliteVolSurfaceRepository repo(db_path.string());

    numeraire::database::VolSurfaceEodHeaderWrite hdr{};
    hdr.underlying_id = "NDX";
    hdr.as_of = "2026-05-15";
    hdr.surface_kind = "implied_bs_eod";
    hdr.spot_used = 28000.0;
    hdr.risk_free_rate = 0.03;
    hdr.dividend_yield = 0.0;
    hdr.ingested_at = "2026-05-16T00:00:00Z";
    hdr.batch_run_id = "unit-test";

    std::vector<numeraire::database::VolSurfacePointWrite> points;

    auto add = [&](const char* expiry, double tau, double strike, const char* type, double iv) {
        numeraire::database::VolSurfacePointWrite p{};
        p.expiration_date = expiry;
        p.years_to_maturity = tau;
        p.strike = strike;
        p.contract_type = type;
        p.implied_vol = iv;
        p.quality = "ok";
        points.push_back(p);
    };

    add("2026-06-15", 0.10, 28000.0, "call", 0.20);
    add("2026-06-15", 0.10, 29400.0, "call", 0.22);
    add("2026-06-15", 0.10, 26600.0, "put", 0.23);
    add("2026-09-15", 0.35, 28000.0, "call", 0.21);
    add("2026-09-15", 0.35, 29400.0, "call", 0.24);

    (void)repo.UpsertSurface(hdr, points);
}

}  // namespace

TEST(SqliteVolSurfaceMarketDataTest, LoadsAndInterpolatesCallVol) {
    const fs::path db_path = fs::temp_directory_path() / "numeraire_sqlite_vol_surface_md_test.sqlite3";
    std::error_code ec;
    fs::remove(db_path, ec);

    SQLite::Database db(db_path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec(ReadSchemaFile());
    SeedSurface(db_path);

    const auto valuation = numeraire::schedule::ParseIsoDate("2026-05-15");
    std::unordered_map<std::string, double> spots{{"NDX", 28000.0}};

    auto market = numeraire::market_data::SqliteVolSurfaceMarketData::Load(
            db_path.string(), valuation, spots, 0.03, {}, {"NDX"}, "2026-05-15");

    const double iv_atm_short = market->ImpliedVolatility("NDX", 28000.0, 0.10, numeraire::OptionType::kCall);
    EXPECT_NEAR(iv_atm_short, 0.20, 1.0e-6);

    const double iv_otm_short = market->ImpliedVolatility("NDX", 29400.0, 0.10, numeraire::OptionType::kCall);
    EXPECT_NEAR(iv_otm_short, 0.22, 1.0e-6);

    const double iv_put_short = market->ImpliedVolatility("NDX", 26600.0, 0.10, numeraire::OptionType::kPut);
    EXPECT_NEAR(iv_put_short, 0.23, 1.0e-6);

    const double iv_atm_long = market->ImpliedVolatility("NDX", 28000.0, 0.35, numeraire::OptionType::kCall);
    EXPECT_NEAR(iv_atm_long, 0.21, 1.0e-6);

    fs::remove(db_path, ec);
}

TEST(SqliteVolSurfaceMarketDataTest, MissingSurfaceThrows) {
    const fs::path db_path = fs::temp_directory_path() / "numeraire_sqlite_vol_surface_md_empty.sqlite3";
    std::error_code ec;
    fs::remove(db_path, ec);

    SQLite::Database db(db_path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec(ReadSchemaFile());

    const auto valuation = numeraire::schedule::ParseIsoDate("2026-05-15");
    EXPECT_THROW(numeraire::market_data::SqliteVolSurfaceMarketData::Load(
                         db_path.string(), valuation, {{"NDX", 1.0}}, 0.03, {}, {"NDX"}, "2026-05-15"),
                 numeraire::ValidationError);

    fs::remove(db_path, ec);
}
