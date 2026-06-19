#pragma once

#include <numeraire/simulation/exposure_time_grid.hpp>
#include <numeraire/simulation/gbm_spec.hpp>
#include <numeraire/simulation/random_engine.hpp>
#include <numeraire/simulation/scenario_buffer.hpp>

namespace numeraire::simulation {

/// Evolve one risk factor (`factor = 0`) along `time_grid` with exact GBM steps
/// between consecutive grid nodes:
///
/// `S(t_{k+1}) = S(t_k) * exp((r - q - 0.5*sigma^2)*dt + sigma*sqrt(dt)*Z)`.
///
/// Writes `spec.spot` to step `0` for every path, then fills steps `1..K-1`.
/// Requires `buffer.NumFactors() == 1` and `buffer.NumSteps() == time_grid.NumSteps()`.
/// Uses flat `r`, `q`, `sigma` over the horizon (tenor-dependent curve in a later step).
void EvolveSingleFactorGbm(ScenarioBuffer& buffer,
                           const ExposureTimeGrid& time_grid,
                           const SingleFactorGbmSpec& spec,
                           IRandomEngine& engine);

}  // namespace numeraire::simulation
