#include <gtest/gtest.h>

#include <cmath>
#include <numeraire/quant/cholesky.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/schedule/format_iso_date.hpp>
#include <numeraire/simulation/exposure_time_grid.hpp>
#include <numeraire/simulation/gbm_evolution.hpp>
#include <numeraire/simulation/gbm_spec.hpp>
#include <numeraire/simulation/historical_calibrator.hpp>
#include <numeraire/simulation/random_engine.hpp>
#include <numeraire/simulation/scenario_buffer.hpp>
#include <numeraire/utils/exception.hpp>

#include <unordered_map>
#include <vector>

namespace {

using numeraire::quant::CholeskyFactor;
using numeraire::schedule::AddCalendarDays;
using numeraire::schedule::FormatIsoDate;
using numeraire::schedule::ParseIsoDate;
using numeraire::simulation::BuildExposureTimeGrid;
using numeraire::simulation::BuildMultiFactorGbmSpec;
using numeraire::simulation::CalibrateFromPriceHistory;
using numeraire::simulation::EvolveMultiFactorGbm;
using numeraire::simulation::EvolveSingleFactorGbm;
using numeraire::simulation::ExposureGridConfig;
using numeraire::simulation::ExposureTimeGrid;
using numeraire::simulation::HistoricalCalibratorConfig;
using numeraire::simulation::MersenneTwisterEngine;
using numeraire::simulation::MultiFactorGbmSpec;
using numeraire::simulation::ScenarioBuffer;
using numeraire::simulation::SingleFactorGbmSpec;

using numeraire::database::DailyCloseObservation;

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

[[nodiscard]] CholeskyFactor TwoByTwoCholesky(const double rho) {
    CholeskyFactor factor;
    factor.n = 2;
    factor.lower = {1.0, 0.0, rho, std::sqrt(std::max(0.0, 1.0 - (rho * rho)))};
    return factor;
}

[[nodiscard]] double PearsonCorrelation(const std::vector<double>& x, const std::vector<double>& y) {
    const std::size_t n = x.size();
    double mean_x = 0.0;
    double mean_y = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        mean_x += x[i];
        mean_y += y[i];
    }
    mean_x /= static_cast<double>(n);
    mean_y /= static_cast<double>(n);

    double cov = 0.0;
    double var_x = 0.0;
    double var_y = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double dx = x[i] - mean_x;
        const double dy = y[i] - mean_y;
        cov += dx * dy;
        var_x += dx * dx;
        var_y += dy * dy;
    }
    return cov / std::sqrt(var_x * var_y);
}

[[nodiscard]] std::vector<DailyCloseObservation> BuildSeries(const numeraire::schedule::Date& start,
                                                             const int num_days,
                                                             const double start_price,
                                                             const double daily_shock_scale) {
    std::vector<DailyCloseObservation> out;
    out.reserve(static_cast<std::size_t>(num_days));
    numeraire::schedule::Date date = start;
    double price = start_price;
    for (int day = 0; day < num_days; ++day) {
        out.push_back(DailyCloseObservation{.as_of = FormatIsoDate(date), .close = price});
        const double shock = daily_shock_scale * std::sin(0.17 * static_cast<double>(day));
        price *= std::exp(shock);
        date = AddCalendarDays(date, 1);
    }
    return out;
}

TEST(MultiFactorGbmEvolutionTest, StepZeroHasPerFactorSpots) {
    const auto grid = SimpleGrid();
    ScenarioBuffer buffer(2, grid.NumSteps(), 16);
    MersenneTwisterEngine engine(123);
    const MultiFactorGbmSpec spec{
            .spots = {100.0, 200.0},
            .risk_free_rates = {0.03, 0.03},
            .dividend_yields = {0.01, 0.0},
            .volatilities = {0.20, 0.25},
            .cholesky = TwoByTwoCholesky(0.5),
    };
    EvolveMultiFactorGbm(buffer, grid, spec, engine);

    for (std::size_t path = 0; path < buffer.NumPaths(); ++path) {
        EXPECT_DOUBLE_EQ(buffer.At(0, 0, path), spec.spots[0]);
        EXPECT_DOUBLE_EQ(buffer.At(1, 0, path), spec.spots[1]);
    }
}

TEST(MultiFactorGbmEvolutionTest, ReproducibleForFixedSeed) {
    const auto grid = SimpleGrid();
    const MultiFactorGbmSpec spec{
            .spots = {50.0, 75.0},
            .risk_free_rates = {0.02, 0.02},
            .dividend_yields = {0.0, 0.01},
            .volatilities = {0.25, 0.30},
            .cholesky = TwoByTwoCholesky(0.65),
    };
    ScenarioBuffer a(2, grid.NumSteps(), 128);
    ScenarioBuffer b(2, grid.NumSteps(), 128);
    MersenneTwisterEngine engine_a(999);
    MersenneTwisterEngine engine_b(999);
    EvolveMultiFactorGbm(a, grid, spec, engine_a);
    EvolveMultiFactorGbm(b, grid, spec, engine_b);

    for (std::size_t factor = 0; factor < 2; ++factor) {
        for (std::size_t step = 0; step < grid.NumSteps(); ++step) {
            const auto slab_a = a.Slab(factor, step);
            const auto slab_b = b.Slab(factor, step);
            for (std::size_t path = 0; path < slab_a.size(); ++path) {
                EXPECT_DOUBLE_EQ(slab_a[path], slab_b[path]);
            }
        }
    }
}

TEST(MultiFactorGbmEvolutionTest, TerminalMeanMatchesGbmExpectationPerFactor) {
    const auto grid = SimpleGrid();
    const std::size_t num_paths = 200000;
    ScenarioBuffer buffer(2, grid.NumSteps(), num_paths);
    MersenneTwisterEngine engine(2024);
    const MultiFactorGbmSpec spec{
            .spots = {100.0, 50.0},
            .risk_free_rates = {0.04, 0.03},
            .dividend_yields = {0.01, 0.02},
            .volatilities = {0.20, 0.15},
            .cholesky = TwoByTwoCholesky(0.4),
    };
    EvolveMultiFactorGbm(buffer, grid, spec, engine);

    const std::size_t terminal_step = grid.NumSteps() - 1;
    const double terminal_tau = grid.nodes[terminal_step].year_fraction;
    for (std::size_t factor = 0; factor < 2; ++factor) {
        double sum = 0.0;
        for (std::size_t path = 0; path < num_paths; ++path) {
            sum += buffer.At(factor, terminal_step, path);
        }
        const double mean_terminal = sum / static_cast<double>(num_paths);
        const double expected =
                spec.spots[factor] *
                std::exp((spec.risk_free_rates[factor] - spec.dividend_yields[factor]) * terminal_tau);
        EXPECT_NEAR(mean_terminal, expected, expected * 0.02);
    }
}

TEST(MultiFactorGbmEvolutionTest, PerfectCorrelationProducesIdenticalLogReturns) {
    const auto grid = SimpleGrid();
    const std::size_t num_paths = 50000;
    ScenarioBuffer buffer(2, grid.NumSteps(), num_paths);
    MersenneTwisterEngine engine(42);
    const MultiFactorGbmSpec spec{
            .spots = {100.0, 100.0},
            .risk_free_rates = {0.02, 0.02},
            .dividend_yields = {0.0, 0.0},
            .volatilities = {0.25, 0.25},
            .cholesky = TwoByTwoCholesky(1.0),
    };
    EvolveMultiFactorGbm(buffer, grid, spec, engine);

    const std::size_t terminal_step = grid.NumSteps() - 1;
    std::vector<double> log_ret_0;
    std::vector<double> log_ret_1;
    log_ret_0.reserve(num_paths);
    log_ret_1.reserve(num_paths);
    for (std::size_t path = 0; path < num_paths; ++path) {
        log_ret_0.push_back(std::log(buffer.At(0, terminal_step, path) / spec.spots[0]));
        log_ret_1.push_back(std::log(buffer.At(1, terminal_step, path) / spec.spots[1]));
    }
    EXPECT_NEAR(PearsonCorrelation(log_ret_0, log_ret_1), 1.0, 0.02);
}

TEST(MultiFactorGbmEvolutionTest, EmpiricalCorrelationNearTarget) {
    const auto grid = SimpleGrid();
    const std::size_t num_paths = 250000;
    ScenarioBuffer buffer(2, grid.NumSteps(), num_paths);
    MersenneTwisterEngine engine(31415);
    constexpr double target_rho = 0.75;
    const MultiFactorGbmSpec spec{
            .spots = {80.0, 120.0},
            .risk_free_rates = {0.03, 0.03},
            .dividend_yields = {0.01, 0.01},
            .volatilities = {0.22, 0.22},
            .cholesky = TwoByTwoCholesky(target_rho),
    };
    EvolveMultiFactorGbm(buffer, grid, spec, engine);

    const std::size_t terminal_step = grid.NumSteps() - 1;
    std::vector<double> log_ret_0;
    std::vector<double> log_ret_1;
    log_ret_0.reserve(num_paths);
    log_ret_1.reserve(num_paths);
    for (std::size_t path = 0; path < num_paths; ++path) {
        log_ret_0.push_back(std::log(buffer.At(0, terminal_step, path) / spec.spots[0]));
        log_ret_1.push_back(std::log(buffer.At(1, terminal_step, path) / spec.spots[1]));
    }
    EXPECT_NEAR(PearsonCorrelation(log_ret_0, log_ret_1), target_rho, 0.03);
}

TEST(MultiFactorGbmEvolutionTest, SingleFactorMatchesDedicatedKernel) {
    const auto grid = SimpleGrid();
    const std::size_t num_paths = 256;
    ScenarioBuffer single_buffer(1, grid.NumSteps(), num_paths);
    ScenarioBuffer multi_buffer(1, grid.NumSteps(), num_paths);
    MersenneTwisterEngine engine_single(777);
    MersenneTwisterEngine engine_multi(777);

    const SingleFactorGbmSpec single_spec{.spot = 120.0,
                                          .risk_free_rate = 0.035,
                                          .dividend_yield = 0.005,
                                          .volatility = 0.18};
    const MultiFactorGbmSpec multi_spec{
            .spots = {120.0},
            .risk_free_rates = {0.035},
            .dividend_yields = {0.005},
            .volatilities = {0.18},
            .cholesky = CholeskyFactor{.n = 1, .lower = {1.0}},
    };

    EvolveSingleFactorGbm(single_buffer, grid, single_spec, engine_single);
    EvolveMultiFactorGbm(multi_buffer, grid, multi_spec, engine_multi);

    for (std::size_t step = 0; step < grid.NumSteps(); ++step) {
        const auto single_slab = single_buffer.Slab(0, step);
        const auto multi_slab = multi_buffer.Slab(0, step);
        for (std::size_t path = 0; path < num_paths; ++path) {
            EXPECT_DOUBLE_EQ(single_slab[path], multi_slab[path]);
        }
    }
}

TEST(MultiFactorGbmEvolutionTest, RejectsDimensionMismatch) {
    const auto grid = SimpleGrid();
    ScenarioBuffer buffer(2, grid.NumSteps(), 4);
    MersenneTwisterEngine engine(1);
    const MultiFactorGbmSpec spec{
            .spots = {1.0},
            .risk_free_rates = {0.02},
            .dividend_yields = {0.0},
            .volatilities = {0.2},
            .cholesky = CholeskyFactor{.n = 1, .lower = {1.0}},
    };
    EXPECT_THROW(EvolveMultiFactorGbm(buffer, grid, spec, engine), numeraire::ValidationError);
}

TEST(MultiFactorGbmSpecTest, BuildsFromHistoricalCalibration) {
    const auto start = ParseIsoDate("2024-01-02");
    const int num_days = 120;
    std::unordered_map<std::string, std::vector<DailyCloseObservation>> closes_by_factor;
    closes_by_factor.emplace("AAPL", BuildSeries(start, num_days, 100.0, 0.012));
    closes_by_factor.emplace("MSFT", BuildSeries(start, num_days, 200.0, 0.012));

    HistoricalCalibratorConfig config;
    config.as_of = AddCalendarDays(start, num_days - 1);
    config.min_return_observations = 60;

    const auto calibration = CalibrateFromPriceHistory({"AAPL", "MSFT"}, closes_by_factor, config);
    const MultiFactorGbmSpec spec = BuildMultiFactorGbmSpec(calibration, 0.04, 0.01);

    ASSERT_EQ(spec.NumFactors(), 2U);
    EXPECT_EQ(spec.spots, calibration.spots_as_of);
    EXPECT_EQ(spec.volatilities, calibration.volatilities);
    EXPECT_EQ(spec.cholesky.n, 2U);
    EXPECT_DOUBLE_EQ(spec.risk_free_rates[0], 0.04);
    EXPECT_DOUBLE_EQ(spec.dividend_yields[1], 0.01);
}

}  // namespace
