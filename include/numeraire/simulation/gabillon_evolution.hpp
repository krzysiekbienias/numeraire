#pragma once

#include <numeraire/quant/cholesky.hpp>
#include <numeraire/simulation/exposure_time_grid.hpp>
#include <numeraire/simulation/gabillon_spec.hpp>
#include <numeraire/simulation/random_engine.hpp>
#include <numeraire/simulation/scenario_buffer.hpp>

namespace numeraire::simulation {

/// Correlation between shocks `i` and `j` implied by a Cholesky factor.
///
/// Reading it back out of `L` rather than off a stored correlation matrix is what keeps
/// the Ito drift consistent with the shocks actually drawn. Higham may have nudged the
/// matrix on its way to positive definiteness, and a drift built from the pre-nudge number
/// would leave every futures price with a small spurious trend.
[[nodiscard]] double ShockCorrelationFromCholesky(const quant::CholeskyFactor& factor, std::size_t i,
                                                  std::size_t j);

/// Evolve every dated contract in `spec` along `time_grid` under a two-factor Gabillon fit.
///
/// A futures price is a martingale, so each contract diffuses with no drift beyond the
/// Ito correction:
///
/// \(d\ln F(t,T) = -\tfrac12\sigma_F(\tau)^2 dt + \sigma_S e^{-k\tau} dW_S
///                 + \sigma_L (1-e^{-k\tau}) dW_L\), with \(\tau = T-t\).
///
/// This is the substantive difference from evolving constant-maturity pillars under GBM.
/// A pillar strip gives a contract the dynamics of whatever maturity it currently sits at
/// and lets it drift down today's curve as it ages, which for a seasonal commodity walks a
/// December contract into shoulder-season prices. Here the contract keeps its own anchor
/// and only its *volatility* changes as \(\tau\) shrinks — the Samuelson effect, which is
/// the part that is real.
///
/// Contracts of one curve share its two shocks, so their correlation is a consequence of
/// the model rather than a separately measured matrix. `spec.cholesky` correlates the
/// curves with each other.
///
/// Writes each contract's anchor to step `0`, then fills steps `1..K-1`. A contract that
/// settles inside the grid stops moving at its settlement date.
/// Requires `buffer.NumFactors() == spec.contracts.size()` and
/// `buffer.NumSteps() == time_grid.NumSteps()`.
void EvolveGabillonCurves(ScenarioBuffer& buffer,
                          const ExposureTimeGrid& time_grid,
                          const GabillonSimulationSpec& spec,
                          IRandomEngine& engine);

}  // namespace numeraire::simulation
