#pragma once

#include <numeraire/quant/cholesky.hpp>
#include <numeraire/simulation/commodity_curve_resolver.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace numeraire::simulation {

/// The two state variables of one commodity curve, as calibrated.
struct GabillonCurveParams {
    std::string product_code;
    double mean_reversion{0.0};
    double short_factor_vol{0.0};
    double long_factor_vol{0.0};
};

/// One dated contract carried along the paths.
struct GabillonContract {
    std::string contract_ticker;
    std::size_t curve_index{0};
    /// Act/365 years from the valuation date to settlement. The contract stops moving
    /// once the grid passes it.
    double settlement_years{0.0};
    /// The settle this contract actually printed on the valuation date.
    ///
    /// Simulating from the observed price rather than a fitted one is what makes today's
    /// curve reproduce exactly, seasonal humps and all. A two-parameter base curve misses
    /// the front of a steep strip by several percent, and a booked leg must not start life
    /// several percent away from where it is marked.
    double anchor_price{0.0};
};

/// Everything needed to evolve a commodity book under a two-factor Gabillon fit.
///
/// One buffer row per dated contract, not per constant-maturity pillar. The contracts of
/// a curve share its two shocks, so their co-movement comes out of the model instead of a
/// measured pillar-to-pillar matrix, and each one keeps its own delivery month for good —
/// a December gas contract never slides down the curve into shoulder-season pricing.
struct GabillonSimulationSpec {
    std::vector<GabillonCurveParams> curves;
    std::vector<GabillonContract> contracts;
    /// Over `2 * curves.size()` shocks, ordered short then long within each curve,
    /// matching `curves`.
    quant::CholeskyFactor cholesky;
    std::string calibration_as_of;
    std::int64_t calibration_id{0};

    [[nodiscard]] std::size_t NumFactors() const noexcept { return contracts.size(); }
    [[nodiscard]] std::size_t NumShocks() const noexcept { return curves.size() * 2U; }
};

/// One curve's calibrated dynamics, including the within-curve factor correlation that a
/// `GabillonSimulationSpec` otherwise keeps folded into its Cholesky.
struct GabillonCurveDynamics {
    GabillonCurveParams params;
    double factor_correlation{0.0};
};

/// Calibrated dynamics of a single `product_code` from the latest `gabillon_2f` / `fit`
/// snapshot for `scope_key`.
///
/// Pulled out on its own so a curve can be studied away from the book it was calibrated
/// with — as a starting point for hand-set parameters, or to re-run the fitted ones over a
/// longer strip than anyone happens to have booked.
///
/// Returns `std::nullopt` when there is no such snapshot or it does not carry that curve.
[[nodiscard]] std::optional<GabillonCurveDynamics> TryLoadGabillonCurveDynamics(
        const std::string& database_file_path,
        std::string_view scope_key,
        std::string_view as_of,
        std::string_view product_code);

/// Assemble a spec for the LIVE futures legs of `scope_key` from the latest
/// `gabillon_2f` / `fit` snapshot with `as_of <= valuation_as_of`.
///
/// Returns `std::nullopt` when no such snapshot exists. Throws when one exists but the
/// book cannot be simulated from it — a curve with no calibrated factors, or a contract
/// with no settle on the valuation date — because both would otherwise show up as a
/// quietly mispriced exposure rather than a failure.
[[nodiscard]] std::optional<GabillonSimulationSpec> TryLoadGabillonSpecFromDatabase(
        const std::string& database_file_path,
        std::string_view scope_key,
        std::string_view valuation_as_of);

}  // namespace numeraire::simulation
