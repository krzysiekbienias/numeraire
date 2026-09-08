#include <gtest/gtest.h>

#include <numeraire/quant/cholesky.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/simulation/exposure_time_grid.hpp>
#include <numeraire/simulation/gabillon_evolution.hpp>
#include <numeraire/simulation/gabillon_spec.hpp>
#include <numeraire/simulation/random_engine.hpp>
#include <numeraire/simulation/scenario_buffer.hpp>
#include <numeraire/utils/exception.hpp>

#include <cmath>
#include <cstddef>
#include <tuple>
#include <vector>

namespace {

using numeraire::ValidationError;
using numeraire::quant::CholeskyDecompose;
using numeraire::quant::CholeskyFactor;
using numeraire::quant::CholeskyStatus;
using numeraire::simulation::EvolveGabillonCurves;
using numeraire::simulation::ExposureGridNode;
using numeraire::simulation::ExposureTimeGrid;
using numeraire::simulation::GabillonContract;
using numeraire::simulation::GabillonCurveParams;
using numeraire::simulation::GabillonSimulationSpec;
using numeraire::simulation::MersenneTwisterEngine;
using numeraire::simulation::ScenarioBuffer;

constexpr std::size_t kNumPaths = 40000;

/// Quarterly nodes out `num_years`, which is enough resolution for the loadings to move
/// noticeably between steps.
[[nodiscard]] ExposureTimeGrid QuarterlyGrid(const double num_years) {
    ExposureTimeGrid grid;
    grid.valuation_date = numeraire::schedule::Date{.year = 2026, .month = 9, .day = 2};
    for (int quarter = 0; static_cast<double>(quarter) * 0.25 <= num_years; ++quarter) {
        ExposureGridNode node;
        node.year_fraction = static_cast<double>(quarter) * 0.25;
        node.date = numeraire::schedule::AddCalendarDays(
                grid.valuation_date, static_cast<int>(node.year_fraction * 365.0));
        grid.nodes.push_back(node);
    }
    return grid;
}

[[nodiscard]] CholeskyFactor FactorOf(const std::vector<double>& correlation, const std::size_t n) {
    const auto result = CholeskyDecompose(correlation, n);
    EXPECT_EQ(result.status, CholeskyStatus::kOk);
    return result.factor;
}

/// One curve, two contracts at different maturities.
[[nodiscard]] GabillonSimulationSpec OneCurveSpec(const double correlation = 0.5) {
    GabillonSimulationSpec spec;
    spec.curves.push_back(GabillonCurveParams{.product_code = "NG",
                                              .mean_reversion = 3.0,
                                              .short_factor_vol = 0.80,
                                              .long_factor_vol = 0.30});
    spec.contracts.push_back(GabillonContract{.contract_ticker = "NG_NEAR",
                                              .curve_index = 0,
                                              .settlement_years = 0.25,
                                              .anchor_price = 3.55});
    spec.contracts.push_back(GabillonContract{.contract_ticker = "NG_FAR",
                                              .curve_index = 0,
                                              .settlement_years = 2.5,
                                              .anchor_price = 4.10});
    spec.cholesky = FactorOf({1.0, correlation, correlation, 1.0}, 2U);
    return spec;
}

[[nodiscard]] double MeanAt(const ScenarioBuffer& buffer, const std::size_t factor, const std::size_t step) {
    double sum = 0.0;
    for (std::size_t path = 0; path < buffer.NumPaths(); ++path) {
        sum += buffer.At(factor, step, path);
    }
    return sum / static_cast<double>(buffer.NumPaths());
}

[[nodiscard]] double LogStdDevAt(const ScenarioBuffer& buffer, const std::size_t factor,
                                 const std::size_t step) {
    double mean = 0.0;
    for (std::size_t path = 0; path < buffer.NumPaths(); ++path) {
        mean += std::log(buffer.At(factor, step, path));
    }
    mean /= static_cast<double>(buffer.NumPaths());
    double sum_sq = 0.0;
    for (std::size_t path = 0; path < buffer.NumPaths(); ++path) {
        const double d = std::log(buffer.At(factor, step, path)) - mean;
        sum_sq += d * d;
    }
    return std::sqrt(sum_sq / static_cast<double>(buffer.NumPaths() - 1U));
}

[[nodiscard]] double LogCorrelationAt(const ScenarioBuffer& buffer, const std::size_t a, const std::size_t b,
                                      const std::size_t step) {
    const auto n = static_cast<double>(buffer.NumPaths());
    double mean_a = 0.0;
    double mean_b = 0.0;
    for (std::size_t path = 0; path < buffer.NumPaths(); ++path) {
        mean_a += std::log(buffer.At(a, step, path));
        mean_b += std::log(buffer.At(b, step, path));
    }
    mean_a /= n;
    mean_b /= n;
    double cov = 0.0;
    double var_a = 0.0;
    double var_b = 0.0;
    for (std::size_t path = 0; path < buffer.NumPaths(); ++path) {
        const double da = std::log(buffer.At(a, step, path)) - mean_a;
        const double db = std::log(buffer.At(b, step, path)) - mean_b;
        cov += da * db;
        var_a += da * da;
        var_b += db * db;
    }
    return cov / std::sqrt(var_a * var_b);
}

void EvolveWithFixedSeed(ScenarioBuffer& buffer, const ExposureTimeGrid& grid, const GabillonSimulationSpec& spec) {
    MersenneTwisterEngine engine(20260902U);
    EvolveGabillonCurves(buffer, grid, spec, engine);
}

}  // namespace

TEST(GabillonEvolutionTest, EveryPathStartsOnTheContractsOwnSettle) {
    const GabillonSimulationSpec spec = OneCurveSpec();
    const ExposureTimeGrid grid = QuarterlyGrid(1.0);
    ScenarioBuffer buffer(spec.NumFactors(), grid.NumSteps(), kNumPaths);
    EvolveWithFixedSeed(buffer, grid, spec);

    // Anchoring on the observed settle rather than a fitted level is what reproduces
    // today's curve exactly, seasonal shape included.
    for (std::size_t path = 0; path < buffer.NumPaths(); ++path) {
        EXPECT_DOUBLE_EQ(buffer.At(0, 0, path), 3.55);
        EXPECT_DOUBLE_EQ(buffer.At(1, 0, path), 4.10);
    }
}

TEST(GabillonEvolutionTest, FuturesPricesAreMartingales) {
    const GabillonSimulationSpec spec = OneCurveSpec();
    const ExposureTimeGrid grid = QuarterlyGrid(2.0);
    ScenarioBuffer buffer(spec.NumFactors(), grid.NumSteps(), kNumPaths);
    EvolveWithFixedSeed(buffer, grid, spec);

    // The whole reason for simulating contracts instead of pillars: a futures price has
    // no drift, so a long position must not accumulate expected profit out of nowhere.
    for (std::size_t factor = 0; factor < spec.NumFactors(); ++factor) {
        const double anchor = spec.contracts[factor].anchor_price;
        for (std::size_t step = 1; step < grid.NumSteps(); ++step) {
            EXPECT_NEAR(MeanAt(buffer, factor, step) / anchor, 1.0, 0.02)
                    << "factor " << factor << " step " << step;
        }
    }
}

TEST(GabillonEvolutionTest, NearContractsAreMoreVolatileThanDeferredOnes) {
    const GabillonSimulationSpec spec = OneCurveSpec();
    const ExposureTimeGrid grid = QuarterlyGrid(1.0);
    ScenarioBuffer buffer(spec.NumFactors(), grid.NumSteps(), kNumPaths);
    EvolveWithFixedSeed(buffer, grid, spec);

    // Samuelson: the front is driven by the short factor, the deferred end mostly by the
    // long one. This is the part of curve dynamics that is real, as opposed to the
    // roll-down a constant-maturity view invents.
    //
    // Compared at node 1, where both contracts have had the same quarter of a year to
    // move. Reading it off a later node would flatter the result, because the near
    // contract settles at 0.25 and stops accumulating while the deferred one carries on.
    const std::size_t step = 1U;
    EXPECT_GT(LogStdDevAt(buffer, 0, step), 1.5 * LogStdDevAt(buffer, 1, step));
}

TEST(GabillonEvolutionTest, ContractsStopMovingOnceTheySettle) {
    GabillonSimulationSpec spec = OneCurveSpec();
    spec.contracts[0].settlement_years = 0.30;
    const ExposureTimeGrid grid = QuarterlyGrid(1.0);
    ScenarioBuffer buffer(spec.NumFactors(), grid.NumSteps(), kNumPaths);
    EvolveWithFixedSeed(buffer, grid, spec);

    // Node 2 sits at 0.50 years, past the contract's settlement, so it must hold whatever
    // it last printed rather than keep diffusing into a price that no longer exists.
    for (std::size_t path = 0; path < buffer.NumPaths(); ++path) {
        EXPECT_DOUBLE_EQ(buffer.At(0, 3, path), buffer.At(0, 2, path));
    }
}

TEST(GabillonEvolutionTest, ContractsOfOneCurveMoveTogetherMoreThanAcrossCurves) {
    GabillonSimulationSpec spec;
    spec.curves.push_back(GabillonCurveParams{
            .product_code = "CL", .mean_reversion = 1.5, .short_factor_vol = 0.54, .long_factor_vol = 0.35});
    spec.curves.push_back(GabillonCurveParams{
            .product_code = "NG", .mean_reversion = 4.7, .short_factor_vol = 0.82, .long_factor_vol = 0.32});
    spec.contracts.push_back(GabillonContract{
            .contract_ticker = "CL_A", .curve_index = 0, .settlement_years = 0.3, .anchor_price = 88.0});
    spec.contracts.push_back(GabillonContract{
            .contract_ticker = "CL_B", .curve_index = 0, .settlement_years = 0.8, .anchor_price = 82.0});
    spec.contracts.push_back(GabillonContract{
            .contract_ticker = "NG_A", .curve_index = 1, .settlement_years = 0.3, .anchor_price = 3.5});

    // Tight within each curve, loose between oil and gas: the shape a real book has.
    const std::vector<double> correlation{
            1.00, 0.85, 0.15, 0.10,
            0.85, 1.00, 0.10, 0.10,
            0.15, 0.10, 1.00, 0.85,
            0.10, 0.10, 0.85, 1.00,
    };
    spec.cholesky = FactorOf(correlation, 4U);

    const ExposureTimeGrid grid = QuarterlyGrid(1.0);
    ScenarioBuffer buffer(spec.NumFactors(), grid.NumSteps(), kNumPaths);
    EvolveWithFixedSeed(buffer, grid, spec);

    // Two contracts on one curve share both its shocks, so they nearly move as one; the
    // gas contract is only loosely tied to either. That structure is inherited from the
    // model rather than measured pair by pair.
    // Not a perfect one, mind: the two maturities weight the short and long factors
    // differently, so they decouple somewhat. What matters is the gap to the gas
    // contract, which is what drives whether this book nets or stacks.
    const std::size_t step = 2U;
    const double within_oil = LogCorrelationAt(buffer, 0, 1, step);
    const double oil_to_gas = LogCorrelationAt(buffer, 0, 2, step);
    EXPECT_GT(within_oil, 0.85);
    EXPECT_LT(oil_to_gas, 0.35);
    EXPECT_GT(oil_to_gas, 0.0);
    EXPECT_GT(within_oil, 2.5 * oil_to_gas);
}

TEST(GabillonEvolutionTest, RejectsSpecsThatCannotBeEvolved) {
    const ExposureTimeGrid grid = QuarterlyGrid(1.0);
    MersenneTwisterEngine engine(1U);

    {
        const GabillonSimulationSpec spec = OneCurveSpec();
        ScenarioBuffer wrong_size(spec.NumFactors() + 1U, grid.NumSteps(), 16U);
        EXPECT_THROW(EvolveGabillonCurves(wrong_size, grid, spec, engine), ValidationError);
    }
    {
        GabillonSimulationSpec spec = OneCurveSpec();
        spec.contracts[0].anchor_price = 0.0;
        ScenarioBuffer buffer(spec.NumFactors(), grid.NumSteps(), 16U);
        EXPECT_THROW(EvolveGabillonCurves(buffer, grid, spec, engine), ValidationError);
    }
    {
        GabillonSimulationSpec spec = OneCurveSpec();
        spec.curves[0].mean_reversion = 0.0;
        ScenarioBuffer buffer(spec.NumFactors(), grid.NumSteps(), 16U);
        EXPECT_THROW(EvolveGabillonCurves(buffer, grid, spec, engine), ValidationError);
    }
    {
        GabillonSimulationSpec spec = OneCurveSpec();
        spec.contracts[1].curve_index = 7U;
        ScenarioBuffer buffer(spec.NumFactors(), grid.NumSteps(), 16U);
        EXPECT_THROW(EvolveGabillonCurves(buffer, grid, spec, engine), ValidationError);
    }
}
