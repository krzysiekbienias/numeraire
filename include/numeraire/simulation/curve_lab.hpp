#pragma once

#include <numeraire/simulation/exposure_time_grid.hpp>
#include <numeraire/simulation/gabillon_spec.hpp>
#include <numeraire/simulation/scenario_buffer.hpp>
#include <numeraire/utils/config.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace numeraire::simulation {

/// What one contract of the studied curve did across the simulation.
struct CurveLabContractStat {
    std::string contract_ticker;
    /// Act/365 years from the valuation date to settlement.
    double settlement_years{0.0};
    double anchor_price{0.0};
    /// Years the contract was actually free to move: the horizon, or its settlement if
    /// that came first.
    double diffusion_years{0.0};
    double mean_terminal{0.0};
    double p5{0.0};
    double p50{0.0};
    double p95{0.0};
    /// Standard deviation of the terminal log move, annualized over `diffusion_years`.
    double realized_vol{0.0};
    /// The same quantity the evolution kernel integrated, from the Gabillon loadings.
    /// Reading close to `realized_vol` is the check that the paths carry the dynamics
    /// the parameters describe.
    double model_vol{0.0};
};

struct CurveLabResult {
    std::vector<CurveLabContractStat> contracts;
    /// Largest relative gap between a contract's mean terminal and its anchor. Futures are
    /// martingales, so this is the honest measure of how much drift the discretisation and
    /// the sampling error let through.
    double worst_martingale_drift{0.0};
};

/// Build a single-curve spec straight from the observed forward curve on `as_of`.
///
/// Independent of any book: the contracts come from what the market quoted, not from what
/// happens to be booked, so a curve can be studied over its whole strip. `dynamics` is
/// taken as given — hand-set or calibrated — which is what makes this usable for asking
/// "what would this curve do if mean reversion were twice as fast".
///
/// Contracts are taken in settlement order and capped at `max_contracts`, since a full gas
/// strip runs to well over a hundred and the buffer is dense.
[[nodiscard]] GabillonSimulationSpec BuildSingleCurveSpec(const std::string& database_file_path,
                                                          std::string_view product_code,
                                                          std::string_view as_of,
                                                          const GabillonCurveDynamics& dynamics,
                                                          std::size_t max_contracts);

/// Per-contract statistics of an evolved single-curve buffer, read at its final node.
[[nodiscard]] CurveLabResult SummarizeCurveLab(const ScenarioBuffer& buffer,
                                               const ExposureTimeGrid& time_grid,
                                               const GabillonSimulationSpec& spec);

void PrintCurveLabUsageLines();

[[nodiscard]] int TryRunCurveLab(int argc, char** argv, const numeraire::utils::Config& cfg);

}  // namespace numeraire::simulation
