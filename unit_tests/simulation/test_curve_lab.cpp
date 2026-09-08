#include <SQLiteCpp/SQLiteCpp.h>
#include <gtest/gtest.h>

#include <numeraire/quant/cholesky.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/schedule/format_iso_date.hpp>
#include <numeraire/simulation/curve_lab.hpp>
#include <numeraire/simulation/gabillon_evolution.hpp>
#include <numeraire/simulation/random_engine.hpp>
#include <numeraire/simulation/scenario_buffer.hpp>
#include <numeraire/utils/exception.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

using numeraire::ValidationError;
using numeraire::quant::CholeskyDecompose;
using numeraire::quant::CholeskyStatus;
using numeraire::schedule::AddCalendarDays;
using numeraire::schedule::Date;
using numeraire::schedule::FormatIsoDate;
using numeraire::simulation::BuildSingleCurveSpec;
using numeraire::simulation::CurveLabResult;
using numeraire::simulation::EvolveGabillonCurves;
using numeraire::simulation::ExposureGridNode;
using numeraire::simulation::ExposureTimeGrid;
using numeraire::simulation::GabillonContract;
using numeraire::simulation::GabillonCurveDynamics;
using numeraire::simulation::GabillonCurveParams;
using numeraire::simulation::GabillonSimulationSpec;
using numeraire::simulation::MersenneTwisterEngine;
using numeraire::simulation::ScenarioBuffer;
using numeraire::simulation::SummarizeCurveLab;

constexpr const char* kAsOf = "2026-09-02";
constexpr std::size_t kNumPaths = 60000;

[[nodiscard]] ExposureTimeGrid QuarterlyGrid(const double num_years) {
    ExposureTimeGrid grid;
    grid.valuation_date = Date{.year = 2026, .month = 9, .day = 2};
    for (int quarter = 0; static_cast<double>(quarter) * 0.25 <= num_years; ++quarter) {
        ExposureGridNode node;
        node.year_fraction = static_cast<double>(quarter) * 0.25;
        node.date = AddCalendarDays(grid.valuation_date, static_cast<int>(node.year_fraction * 365.0));
        grid.nodes.push_back(node);
    }
    return grid;
}

[[nodiscard]] GabillonSimulationSpec ThreePointCurve() {
    GabillonSimulationSpec spec;
    spec.curves.push_back(GabillonCurveParams{.product_code = "NG",
                                              .mean_reversion = 3.0,
                                              .short_factor_vol = 0.80,
                                              .long_factor_vol = 0.30});
    spec.contracts.push_back(GabillonContract{
            .contract_ticker = "NG_NEAR", .curve_index = 0U, .settlement_years = 0.25, .anchor_price = 3.55});
    spec.contracts.push_back(GabillonContract{
            .contract_ticker = "NG_MID", .curve_index = 0U, .settlement_years = 1.00, .anchor_price = 3.90});
    spec.contracts.push_back(GabillonContract{
            .contract_ticker = "NG_FAR", .curve_index = 0U, .settlement_years = 2.00, .anchor_price = 4.10});

    const std::array<double, 4> correlation{1.0, 0.5, 0.5, 1.0};
    const auto factor = CholeskyDecompose(correlation, 2U);
    EXPECT_EQ(factor.status, CholeskyStatus::kOk);
    spec.cholesky = factor.factor;
    return spec;
}

[[nodiscard]] CurveLabResult RunLab(const GabillonSimulationSpec& spec, const ExposureTimeGrid& grid) {
    ScenarioBuffer buffer(spec.NumFactors(), grid.NumSteps(), kNumPaths);
    MersenneTwisterEngine engine(20260902U);
    EvolveGabillonCurves(buffer, grid, spec, engine);
    return SummarizeCurveLab(buffer, grid, spec);
}

[[nodiscard]] std::string ReadSchemaFile() {
    const fs::path schema = fs::path(NUMERAIRE_SOURCE_DIR) / "sql" / "schema_v1.sql";
    std::ifstream in(schema);
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

[[nodiscard]] fs::path UniqueSqlitePath() {
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
    return fs::temp_directory_path() / ("numeraire_curve_lab_ut_" + std::to_string(ns) + ".sqlite3");
}

/// A monthly strip of `count` contracts, priced so the level is easy to assert on.
void SeedCurve(const fs::path& db_path, const int count) {
    SQLite::Database db(db_path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec(ReadSchemaFile());

    const Date as_of = numeraire::schedule::ParseIsoDate(kAsOf);
    for (int i = 1; i <= count; ++i) {
        const std::string ticker = "NGX" + std::to_string(i);
        const std::string settlement = FormatIsoDate(AddCalendarDays(as_of, 30 * i));
        const double price = 3.0 + (0.1 * static_cast<double>(i));

        SQLite::Statement contract(db,
                                   "INSERT INTO futures_contract (ticker, listing_as_of, product_code, "
                                   "settlement_date, source, ingested_at) "
                                   "VALUES (?, ?, 'NG', ?, 'test', ?)");
        contract.bind(1, ticker);
        contract.bind(2, std::string(kAsOf));
        contract.bind(3, settlement);
        contract.bind(4, std::string(kAsOf));
        contract.exec();

        SQLite::Statement eod(db,
                              "INSERT INTO futures_daily_eod (ticker, product_code, as_of, open, high, low, "
                              "close, settlement_price, source, ingested_at) "
                              "VALUES (?, 'NG', ?, ?, ?, ?, ?, ?, 'test', ?)");
        eod.bind(1, ticker);
        eod.bind(2, std::string(kAsOf));
        eod.bind(3, price);
        eod.bind(4, price);
        eod.bind(5, price);
        eod.bind(6, price);
        eod.bind(7, price);
        eod.bind(8, std::string(kAsOf));
        eod.exec();
    }
}

}  // namespace

TEST(CurveLabTest, RealizedVolatilityMatchesTheModelItWasSimulatedFrom) {
    const GabillonSimulationSpec spec = ThreePointCurve();
    const CurveLabResult result = RunLab(spec, QuarterlyGrid(1.5));

    // The point of the lab: if the paths carry the dynamics the parameters describe, the
    // spread the sample actually shows has to land on the variance the kernel integrated.
    // A mismatch here means the discretisation is lying about the parameters it was given.
    ASSERT_EQ(result.contracts.size(), 3U);
    for (const auto& contract : result.contracts) {
        EXPECT_GT(contract.model_vol, 0.0) << contract.contract_ticker;
        EXPECT_NEAR(contract.realized_vol / contract.model_vol, 1.0, 0.05) << contract.contract_ticker;
    }
}

TEST(CurveLabTest, ReportsNoDriftBecauseFuturesAreMartingales) {
    const GabillonSimulationSpec spec = ThreePointCurve();
    const CurveLabResult result = RunLab(spec, QuarterlyGrid(1.5));

    EXPECT_LT(result.worst_martingale_drift, 0.01);
    for (const auto& contract : result.contracts) {
        EXPECT_NEAR(contract.mean_terminal / contract.anchor_price, 1.0, 0.01) << contract.contract_ticker;
    }
}

TEST(CurveLabTest, QuantilesBracketTheAnchorAndWidenTowardTheFront) {
    const GabillonSimulationSpec spec = ThreePointCurve();
    const CurveLabResult result = RunLab(spec, QuarterlyGrid(1.5));

    for (const auto& contract : result.contracts) {
        EXPECT_LT(contract.p5, contract.p50) << contract.contract_ticker;
        EXPECT_LT(contract.p50, contract.p95) << contract.contract_ticker;
    }
    // Samuelson, read off the lab's own output: the front of the strip is the volatile end.
    EXPECT_GT(result.contracts[0].model_vol, result.contracts[2].model_vol);
}

TEST(CurveLabTest, BuildsTheStripFromTheObservedCurveInSettlementOrder) {
    const fs::path db_path = UniqueSqlitePath();
    SeedCurve(db_path, 10);

    const GabillonCurveDynamics dynamics{
            .params = GabillonCurveParams{.product_code = "NG",
                                          .mean_reversion = 3.0,
                                          .short_factor_vol = 0.8,
                                          .long_factor_vol = 0.3},
            .factor_correlation = 0.5,
    };
    const GabillonSimulationSpec spec = BuildSingleCurveSpec(db_path.string(), "NG", kAsOf, dynamics, 6U);

    // Capped, ordered by settlement, and anchored on the settle each contract printed —
    // never on a fitted level, which is what lets the lab reproduce today's shape.
    EXPECT_EQ(spec.contracts.size(), 6U);
    for (std::size_t i = 0; i < spec.contracts.size(); ++i) {
        EXPECT_EQ(spec.contracts[i].contract_ticker, "NGX" + std::to_string(i + 1U));
        EXPECT_NEAR(spec.contracts[i].anchor_price, 3.0 + (0.1 * static_cast<double>(i + 1U)), 1.0e-9);
        if (i > 0) {
            EXPECT_GT(spec.contracts[i].settlement_years, spec.contracts[i - 1].settlement_years);
        }
    }
    EXPECT_EQ(spec.curves.size(), 1U);
    EXPECT_EQ(spec.cholesky.n, 2U);

    fs::remove(db_path);
}

TEST(CurveLabTest, RefusesCurvesItCannotAnchor) {
    const fs::path db_path = UniqueSqlitePath();
    SeedCurve(db_path, 3);

    const GabillonCurveDynamics dynamics{
            .params = GabillonCurveParams{.product_code = "CL",
                                          .mean_reversion = 1.5,
                                          .short_factor_vol = 0.5,
                                          .long_factor_vol = 0.3},
            .factor_correlation = 0.0,
    };
    // Nothing quoted for CL on that session, so there is no curve to stand the paths on.
    EXPECT_THROW(static_cast<void>(BuildSingleCurveSpec(db_path.string(), "CL", kAsOf, dynamics, 6U)),
                 ValidationError);

    fs::remove(db_path);
}
