#include <numeraire/simulation/gbm_evolution.hpp>

#include <numeraire/simulation/normal_random.hpp>
#include <numeraire/utils/exception.hpp>

#include <cmath>
#include <span>

namespace numeraire::simulation {
namespace {

constexpr double kTimeTol = 1.0e-12;

void ValidateInputs(const ScenarioBuffer& buffer, const ExposureTimeGrid& time_grid,
                    const SingleFactorGbmSpec& spec) {
    if (buffer.NumFactors() != 1U) {
        throw ValidationError("EvolveSingleFactorGbm: buffer must have exactly one factor.");
    }
    if (buffer.NumSteps() != time_grid.NumSteps()) {
        throw ValidationError("EvolveSingleFactorGbm: buffer steps must match time_grid.NumSteps().");
    }
    if (time_grid.NumSteps() == 0U) {
        throw ValidationError("EvolveSingleFactorGbm: time_grid must not be empty.");
    }
    if (spec.spot <= 0.0 || spec.volatility < 0.0) {
        throw ValidationError("EvolveSingleFactorGbm: spot must be > 0 and volatility >= 0.");
    }
    if (!std::isfinite(spec.risk_free_rate) || !std::isfinite(spec.dividend_yield) ||
        !std::isfinite(spec.volatility)) {
        throw ValidationError("EvolveSingleFactorGbm: rates and volatility must be finite.");
    }
    for (std::size_t k = 1; k < time_grid.NumSteps(); ++k) {
        const double dt = time_grid.nodes[k].year_fraction - time_grid.nodes[k - 1].year_fraction;
        if (dt <= kTimeTol) {
            throw ValidationError("EvolveSingleFactorGbm: time_grid year_fraction must increase strictly.");
        }
    }
}

}  // namespace

void EvolveSingleFactorGbm(ScenarioBuffer& buffer, const ExposureTimeGrid& time_grid,
                           const SingleFactorGbmSpec& spec, IRandomEngine& engine) {
    ValidateInputs(buffer, time_grid, spec);

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

}  // namespace numeraire::simulation
