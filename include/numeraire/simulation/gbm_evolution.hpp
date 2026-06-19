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

/// Evolve `F` correlated risk factors along `time_grid` with exact GBM steps.
///
/// At each step draws independent `Z ~ N(0, I)`, forms correlated shocks
/// `eps = L * Z` via `spec.cholesky`, then for each factor `f`:
///
/// `S_f(t_{k+1}) = S_f(t_k) * exp((r_f - q_f - 0.5*sigma_f^2)*dt + sigma_f*sqrt(dt)*eps_f)`.
///
/// Writes per-factor spots to step `0`, then fills steps `1..K-1`.
/// Requires `buffer.NumFactors() == spec.NumFactors() == spec.cholesky.n`.
void EvolveMultiFactorGbm(ScenarioBuffer& buffer,
                          const ExposureTimeGrid& time_grid,
                          const MultiFactorGbmSpec& spec,
                          IRandomEngine& engine);

}  // namespace numeraire::simulation
