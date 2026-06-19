#include <numeraire/simulation/gbm_evolution.hpp>

#include <numeraire/quant/cholesky.hpp>
#include <numeraire/simulation/normal_random.hpp>
#include <numeraire/utils/exception.hpp>

#include <cmath>
#include <span>
#include <vector>

namespace numeraire::simulation {
namespace {

constexpr double kTimeTol = 1.0e-12;

void ValidateTimeGrid(const ScenarioBuffer& buffer, const ExposureTimeGrid& time_grid,
                      const std::size_t expected_factors) {
    if (buffer.NumFactors() != expected_factors) {
        throw ValidationError("GBM evolution: buffer factor count mismatch.");
    }
    if (buffer.NumSteps() != time_grid.NumSteps()) {
        throw ValidationError("GBM evolution: buffer steps must match time_grid.NumSteps().");
    }
    if (time_grid.NumSteps() == 0U) {
        throw ValidationError("GBM evolution: time_grid must not be empty.");
    }
    for (std::size_t k = 1; k < time_grid.NumSteps(); ++k) {
        const double dt = time_grid.nodes[k].year_fraction - time_grid.nodes[k - 1].year_fraction;
        if (dt <= kTimeTol) {
            throw ValidationError("GBM evolution: time_grid year_fraction must increase strictly.");
        }
    }
}

void ValidateSingleFactorSpec(const SingleFactorGbmSpec& spec) {
    if (spec.spot <= 0.0 || spec.volatility < 0.0) {
        throw ValidationError("EvolveSingleFactorGbm: spot must be > 0 and volatility >= 0.");
    }
    if (!std::isfinite(spec.risk_free_rate) || !std::isfinite(spec.dividend_yield) ||
        !std::isfinite(spec.volatility)) {
        throw ValidationError("EvolveSingleFactorGbm: rates and volatility must be finite.");
    }
}

void ValidateMultiFactorSpec(const MultiFactorGbmSpec& spec) {
    const std::size_t n = spec.NumFactors();
    if (n == 0) {
        throw ValidationError("EvolveMultiFactorGbm: spec must have at least one factor.");
    }
    if (spec.risk_free_rates.size() != n || spec.dividend_yields.size() != n ||
        spec.volatilities.size() != n || spec.cholesky.n != n) {
        throw ValidationError("EvolveMultiFactorGbm: spec vectors have inconsistent lengths.");
    }
    for (std::size_t f = 0; f < n; ++f) {
        if (spec.spots[f] <= 0.0 || spec.volatilities[f] < 0.0) {
            throw ValidationError("EvolveMultiFactorGbm: each spot must be > 0 and volatility >= 0.");
        }
        if (!std::isfinite(spec.risk_free_rates[f]) || !std::isfinite(spec.dividend_yields[f]) ||
            !std::isfinite(spec.volatilities[f])) {
            throw ValidationError("EvolveMultiFactorGbm: rates and volatilities must be finite.");
        }
    }
}

}  // namespace

void EvolveSingleFactorGbm(ScenarioBuffer& buffer, const ExposureTimeGrid& time_grid,
                           const SingleFactorGbmSpec& spec, IRandomEngine& engine) {
    ValidateTimeGrid(buffer, time_grid, 1U);
    ValidateSingleFactorSpec(spec);

    const std::span<double> initial_slab = buffer.Slab(0, 0);
    for (double& spot_path : initial_slab) {
        spot_path = spec.spot;
    }

    StandardNormalGenerator normals(&engine);
    for (std::size_t step = 1; step < time_grid.NumSteps(); ++step) {
        const double dt =
                time_grid.nodes[step].year_fraction - time_grid.nodes[step - 1].year_fraction;
        const double drift = (spec.risk_free_rate - spec.dividend_yield -
                              (0.5 * spec.volatility * spec.volatility)) *
                             dt;
        const double diffusion_scale = spec.volatility * std::sqrt(dt);

        const std::span<const double> previous = buffer.Slab(0, step - 1);
        const std::span<double> current = buffer.Slab(0, step);
        for (std::size_t path = 0; path < buffer.NumPaths(); ++path) {
            const double shock = normals.Next();
            current[path] = previous[path] * std::exp(drift + (diffusion_scale * shock));
        }
    }
}

void EvolveMultiFactorGbm(ScenarioBuffer& buffer, const ExposureTimeGrid& time_grid,
                          const MultiFactorGbmSpec& spec, IRandomEngine& engine) {
    const std::size_t num_factors = spec.NumFactors();
    ValidateTimeGrid(buffer, time_grid, num_factors);
    ValidateMultiFactorSpec(spec);

    for (std::size_t factor = 0; factor < num_factors; ++factor) {
        const std::span<double> initial_slab = buffer.Slab(factor, 0);
        for (double& spot_path : initial_slab) {
            spot_path = spec.spots[factor];
        }
    }

    StandardNormalGenerator normals(&engine);
    std::vector<double> independent_normals(num_factors);
    std::vector<double> correlated_shocks(num_factors);

    for (std::size_t step = 1; step < time_grid.NumSteps(); ++step) {
        const double dt =
                time_grid.nodes[step].year_fraction - time_grid.nodes[step - 1].year_fraction;
        const double sqrt_dt = std::sqrt(dt);

        std::vector<double> drifts(num_factors);
        std::vector<double> diffusion_scales(num_factors);
        for (std::size_t factor = 0; factor < num_factors; ++factor) {
            const double sigma = spec.volatilities[factor];
            drifts[factor] = (spec.risk_free_rates[factor] - spec.dividend_yields[factor] -
                              (0.5 * sigma * sigma)) *
                             dt;
            diffusion_scales[factor] = sigma * sqrt_dt;
        }

        for (std::size_t path = 0; path < buffer.NumPaths(); ++path) {
            normals.Fill(independent_normals);
            quant::ApplyLowerTriangular(spec.cholesky, independent_normals, correlated_shocks);

            for (std::size_t factor = 0; factor < num_factors; ++factor) {
                const double previous = buffer.At(factor, step - 1, path);
                buffer.At(factor, step, path) =
                        previous * std::exp(drifts[factor] + (diffusion_scales[factor] * correlated_shocks[factor]));
            }
        }
    }
}

}  // namespace numeraire::simulation
