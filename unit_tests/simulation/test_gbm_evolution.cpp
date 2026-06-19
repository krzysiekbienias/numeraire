#include <gtest/gtest.h>

#include <cmath>
#include <numeraire/schedule/date.hpp>
#include <numeraire/simulation/exposure_time_grid.hpp>
#include <numeraire/simulation/gbm_evolution.hpp>
#include <numeraire/simulation/random_engine.hpp>
#include <numeraire/simulation/scenario_dump.hpp>
#include <numeraire/utils/exception.hpp>

namespace {

using numeraire::schedule::ParseIsoDate;
using numeraire::simulation::BuildExposureTimeGrid;
using numeraire::simulation::EvolveSingleFactorGbm;
using numeraire::simulation::ExposureGridConfig;
using numeraire::simulation::ExposureTimeGrid;
using numeraire::simulation::MersenneTwisterEngine;
using numeraire::simulation::DumpScenarioPathsIfEnvSet;
using numeraire::simulation::ScenarioBuffer;
using numeraire::simulation::SingleFactorGbmSpec;

[[nodiscard]] ExposureTimeGrid SimpleGrid() {
    ExposureGridConfig cfg;
    cfg.conventions.include_valuation_date = true;
    cfg.exposure_pillars = {
            {.id = "M1", .target_dte_days = 30, .tier = "core"},
            {.id = "M3", .target_dte_days = 90, .tier = "core"},
            {.id = "Y1", .target_dte_days = 365, .tier = "core"},
    };
    return BuildExposureTimeGrid(cfg, ParseIsoDate("2026-01-02"), std::nullopt);
}

TEST(GbmEvolutionTest, StepZeroIsInitialSpot) {
    const auto grid = SimpleGrid();
    ScenarioBuffer buffer(1, grid.NumSteps(), 16);
    MersenneTwisterEngine engine(123);
    const SingleFactorGbmSpec spec{.spot = 100.0,
                                   .risk_free_rate = 0.03,
                                   .dividend_yield = 0.01,
                                   .volatility = 0.20};
    EvolveSingleFactorGbm(buffer, grid, spec, engine);
    for (std::size_t path = 0; path < buffer.NumPaths(); ++path) {
        EXPECT_DOUBLE_EQ(buffer.At(0, 0, path), spec.spot);
    }
}

TEST(GbmEvolutionTest, ReproducibleForFixedSeed) {
    const auto grid = SimpleGrid();
    const SingleFactorGbmSpec spec{.spot = 50.0,
                                   .risk_free_rate = 0.02,
                                   .dividend_yield = 0.0,
                                   .volatility = 0.25};
    ScenarioBuffer a(1, grid.NumSteps(), 128);
    ScenarioBuffer b(1, grid.NumSteps(), 128);
    MersenneTwisterEngine engine_a(999);
    MersenneTwisterEngine engine_b(999);
    EvolveSingleFactorGbm(a, grid, spec, engine_a);
    EvolveSingleFactorGbm(b, grid, spec, engine_b);
    (void)DumpScenarioPathsIfEnvSet(a, grid);
    for (std::size_t step = 0; step < grid.NumSteps(); ++step) {
        const auto slab_a = a.Slab(0, step);
        const auto slab_b = b.Slab(0, step);
        ASSERT_EQ(slab_a.size(), slab_b.size());
        for (std::size_t path = 0; path < slab_a.size(); ++path) {
            EXPECT_DOUBLE_EQ(slab_a[path], slab_b[path]);
        }
    }
}

TEST(GbmEvolutionTest, TerminalMeanMatchesGbmExpectation) {
    const auto grid = SimpleGrid();
    const std::size_t num_paths = 200000;
    ScenarioBuffer buffer(1, grid.NumSteps(), num_paths);
    MersenneTwisterEngine engine(2024);
    const SingleFactorGbmSpec spec{.spot = 100.0,
                                   .risk_free_rate = 0.04,
                                   .dividend_yield = 0.01,
                                   .volatility = 0.20};
    EvolveSingleFactorGbm(buffer, grid, spec, engine);

    const std::size_t terminal_step = grid.NumSteps() - 1;
    const double terminal_tau = grid.nodes[terminal_step].year_fraction;
    double sum = 0.0;
    for (std::size_t path = 0; path < num_paths; ++path) {
        sum += buffer.At(0, terminal_step, path);
    }
    const double mean_terminal = sum / static_cast<double>(num_paths);
    const double expected = spec.spot * std::exp((spec.risk_free_rate - spec.dividend_yield) * terminal_tau);
    EXPECT_NEAR(mean_terminal, expected, expected * 0.02);
}

TEST(GbmEvolutionTest, ZeroVolatilityGivesForwardGrowth) {
    ExposureGridConfig cfg;
    cfg.conventions.include_valuation_date = true;
    cfg.exposure_pillars = {{.id = "M6", .target_dte_days = 180, .tier = "core"}};
    const auto grid = BuildExposureTimeGrid(cfg, ParseIsoDate("2026-01-02"), std::nullopt);
    ScenarioBuffer buffer(1, grid.NumSteps(), 8);
    MersenneTwisterEngine engine(1);
    const SingleFactorGbmSpec spec{.spot = 80.0,
                                   .risk_free_rate = 0.05,
                                   .dividend_yield = 0.02,
                                   .volatility = 0.0};
    EvolveSingleFactorGbm(buffer, grid, spec, engine);

    const std::size_t terminal_step = grid.NumSteps() - 1;
    const double tau = grid.nodes[terminal_step].year_fraction;
    const double expected = spec.spot * std::exp((spec.risk_free_rate - spec.dividend_yield) * tau);
    for (std::size_t path = 0; path < buffer.NumPaths(); ++path) {
        EXPECT_NEAR(buffer.At(0, terminal_step, path), expected, 1.0e-10);
    }
}

TEST(GbmEvolutionTest, RejectsDimensionMismatch) {
    const auto grid = SimpleGrid();
    ScenarioBuffer buffer(2, grid.NumSteps(), 4);
    MersenneTwisterEngine engine(1);
    const SingleFactorGbmSpec spec{.spot = 1.0, .volatility = 0.2};
    EXPECT_THROW(EvolveSingleFactorGbm(buffer, grid, spec, engine), numeraire::ValidationError);
}

}  // namespace
