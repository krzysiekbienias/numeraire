#include <SQLiteCpp/SQLiteCpp.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numeraire/database/historical_calibration_eod_read.hpp>
#include <numeraire/database/historical_calibration_types.hpp>
#include <numeraire/database/sqlite_historical_calibration_repository.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/simulation/exposure_time_grid.hpp>
#include <numeraire/simulation/gbm_evolution.hpp>
#include <numeraire/simulation/historical_calibration_loader.hpp>
#include <numeraire/simulation/random_engine.hpp>
#include <numeraire/simulation/scenario_buffer.hpp>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

using numeraire::database::HasHistoricalCalibrationEod;
using numeraire::database::HistoricalCalibrationCholeskyWrite;
using numeraire::database::HistoricalCalibrationCorrelationWrite;
using numeraire::database::HistoricalCalibrationFactorWrite;
using numeraire::database::HistoricalCalibrationHeaderWrite;
using numeraire::database::SqliteHistoricalCalibrationRepository;
using numeraire::database::TryLoadLatestHistoricalCalibrationEod;
using numeraire::schedule::ParseIsoDate;
using numeraire::simulation::BuildExposureTimeGrid;
using numeraire::simulation::EvolveMultiFactorGbm;
using numeraire::simulation::ExposureGridConfig;
using numeraire::simulation::MersenneTwisterEngine;
using numeraire::simulation::ScenarioBuffer;
using numeraire::simulation::ToHistoricalCalibrationResult;
using numeraire::simulation::TryLoadHistoricalCalibrationFromDatabase;
using numeraire::simulation::TryLoadMultiFactorGbmSpecFromDatabase;

[[nodiscard]] std::string ReadSchemaFile() {
    const fs::path schema = fs::path(NUMERAIRE_SOURCE_DIR) / "sql" / "schema_v1.sql";
    std::ifstream in(schema);
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

[[nodiscard]] fs::path UniqueSqlitePath() {
    using namespace std::chrono;
    const auto ms = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    return fs::temp_directory_path() / ("numeraire_hist_cal_read_ut_" + std::to_string(ms) + ".sqlite3");
}

void UpsertTwoFactorSnapshot(const fs::path& path,
                             const std::string& scope_key,
                             const std::string& as_of,
                             const double rho) {
    SqliteHistoricalCalibrationRepository repo(path.string());

    HistoricalCalibrationHeaderWrite header{};
    header.scope_key = scope_key;
    header.as_of = as_of;
    header.history_start = "2024-01-16";
    header.history_end = as_of;
    header.lookback_calendar_days = 504;
    header.min_return_observations = 60;
    header.num_factors = 2;
    header.num_return_observations = 120;
    header.batch_run_id = "ut-" + scope_key + "-" + as_of;

    const std::vector<HistoricalCalibrationFactorWrite> factors{
            {.factor_index = 0, .underlying_id = "AAPL", .spot_as_of = 190.0, .volatility = 0.22},
            {.factor_index = 1, .underlying_id = "MSFT", .spot_as_of = 420.0, .volatility = 0.18},
    };
    const std::vector<HistoricalCalibrationCorrelationWrite> correlations{
            {.factor_i = 0, .factor_j = 0, .rho = 1.0},
            {.factor_i = 0, .factor_j = 1, .rho = rho},
            {.factor_i = 1, .factor_j = 1, .rho = 1.0},
    };
    const double l10 = rho;
    const double l11 = std::sqrt(std::max(0.0, 1.0 - (rho * rho)));
    const std::vector<HistoricalCalibrationCholeskyWrite> cholesky{
            {.row_i = 0, .col_j = 0, .l_value = 1.0},
            {.row_i = 1, .col_j = 0, .l_value = l10},
            {.row_i = 1, .col_j = 1, .l_value = l11},
    };
    static_cast<void>(repo.UpsertSnapshot(header, factors, correlations, cholesky));
}

}  // namespace

TEST(HistoricalCalibrationEodReadTest, LoadsLatestOnOrBeforeAsOf) {
    const fs::path path = UniqueSqlitePath();
    {
        SQLite::Database db(path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db.exec(ReadSchemaFile());
    }
    UpsertTwoFactorSnapshot(path, "ALL", "2026-06-01", 0.70);
    UpsertTwoFactorSnapshot(path, "ALL", "2026-06-18", 0.80);

    EXPECT_TRUE(HasHistoricalCalibrationEod(path.string(), "ALL", "2026-06-01"));
    EXPECT_FALSE(HasHistoricalCalibrationEod(path.string(), "ALL", "2026-05-31"));

    const auto june_mid = TryLoadLatestHistoricalCalibrationEod(path.string(), "ALL", "2026-06-15");
    ASSERT_TRUE(june_mid.has_value());
    EXPECT_EQ(june_mid->as_of, "2026-06-01");
    EXPECT_EQ(june_mid->factor_ids.size(), 2U);
    EXPECT_NEAR(june_mid->correlation[2], 0.70, 1.0e-9);

    const auto june_end = TryLoadLatestHistoricalCalibrationEod(path.string(), "ALL", "2026-06-18");
    ASSERT_TRUE(june_end.has_value());
    EXPECT_EQ(june_end->as_of, "2026-06-18");
    EXPECT_NEAR(june_end->correlation[2], 0.80, 1.0e-9);

    fs::remove(path);
}

TEST(HistoricalCalibrationLoaderTest, BuildsGbmSpecAndEvolvesPaths) {
    const fs::path path = UniqueSqlitePath();
    {
        SQLite::Database db(path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        db.exec(ReadSchemaFile());
    }
    UpsertTwoFactorSnapshot(path, "BOOK_1", "2026-06-01", 0.75);

    const auto calibration = TryLoadHistoricalCalibrationFromDatabase(path.string(), "BOOK_1", "2026-06-30");
    ASSERT_TRUE(calibration.has_value());
    EXPECT_EQ(calibration->factor_ids[0], "AAPL");
    EXPECT_EQ(calibration->factor_ids[1], "MSFT");
    EXPECT_EQ(calibration->cholesky.n, 2U);

    const auto spec = TryLoadMultiFactorGbmSpecFromDatabase(path.string(), "BOOK_1", "2026-06-30", 0.04, 0.01);
    ASSERT_TRUE(spec.has_value());
    ASSERT_EQ(spec->NumFactors(), 2U);
    EXPECT_DOUBLE_EQ(spec->spots[0], 190.0);
    EXPECT_DOUBLE_EQ(spec->risk_free_rates[1], 0.04);

    ExposureGridConfig cfg;
    cfg.conventions.include_valuation_date = true;
    cfg.exposure_pillars = {{.id = "M3", .target_dte_days = 90, .tier = "core"}};
    const auto grid = BuildExposureTimeGrid(cfg, ParseIsoDate("2026-06-15"), std::nullopt);

    ScenarioBuffer buffer(spec->NumFactors(), grid.NumSteps(), 64);
    MersenneTwisterEngine engine(42);
    EvolveMultiFactorGbm(buffer, grid, *spec, engine);

    for (std::size_t mc_path = 0; mc_path < buffer.NumPaths(); ++mc_path) {
        EXPECT_DOUBLE_EQ(buffer.At(0, 0, mc_path), 190.0);
        EXPECT_DOUBLE_EQ(buffer.At(1, 0, mc_path), 420.0);
        EXPECT_GT(buffer.At(0, grid.NumSteps() - 1, mc_path), 0.0);
        EXPECT_GT(buffer.At(1, grid.NumSteps() - 1, mc_path), 0.0);
    }

    const auto read = TryLoadLatestHistoricalCalibrationEod(path.string(), "BOOK_1", "2026-06-30");
    ASSERT_TRUE(read.has_value());
    const auto round_trip = ToHistoricalCalibrationResult(*read);
    EXPECT_EQ(round_trip.factor_ids, calibration->factor_ids);
    EXPECT_EQ(round_trip.volatilities, calibration->volatilities);
    EXPECT_EQ(round_trip.cholesky.lower, calibration->cholesky.lower);

    fs::remove(path);
}
