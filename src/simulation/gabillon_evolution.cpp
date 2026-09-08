#include <numeraire/simulation/gabillon_evolution.hpp>

#include <numeraire/quant/cholesky.hpp>
#include <numeraire/simulation/normal_random.hpp>
#include <numeraire/utils/exception.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace numeraire::simulation {
namespace {

constexpr double kTimeTol = 1.0e-12;

/// Loadings and drift of one contract over one step; identical on every path, so they are
/// computed once per step rather than once per draw.
struct StepCoefficients {
    double short_load{0.0};
    double long_load{0.0};
    double drift{0.0};
    double sqrt_dt{0.0};
    bool settled{false};
};

void Validate(const ScenarioBuffer& buffer, const ExposureTimeGrid& time_grid,
              const GabillonSimulationSpec& spec) {
    if (spec.curves.empty() || spec.contracts.empty()) {
        throw ValidationError("EvolveGabillonCurves: spec must hold at least one curve and one contract.");
    }
    if (buffer.NumFactors() != spec.contracts.size()) {
        throw ValidationError("EvolveGabillonCurves: buffer needs one factor per dated contract.");
    }
    if (buffer.NumSteps() != time_grid.NumSteps()) {
        throw ValidationError("EvolveGabillonCurves: buffer steps must match time_grid.NumSteps().");
    }
    if (time_grid.NumSteps() == 0U) {
        throw ValidationError("EvolveGabillonCurves: time_grid must not be empty.");
    }
    if (spec.cholesky.n != spec.NumShocks()) {
        throw ValidationError("EvolveGabillonCurves: cholesky must span two shocks per curve.");
    }
    for (std::size_t k = 1; k < time_grid.NumSteps(); ++k) {
        if (time_grid.nodes[k].year_fraction - time_grid.nodes[k - 1].year_fraction <= kTimeTol) {
            throw ValidationError("EvolveGabillonCurves: time_grid year_fraction must increase strictly.");
        }
    }
    for (const GabillonCurveParams& curve : spec.curves) {
        if (!(curve.mean_reversion > 0.0) || !std::isfinite(curve.mean_reversion)) {
            throw ValidationError("EvolveGabillonCurves: mean_reversion must be > 0 for " + curve.product_code +
                                  ".");
        }
        if (!(curve.short_factor_vol >= 0.0) || !(curve.long_factor_vol >= 0.0) ||
            !std::isfinite(curve.short_factor_vol) || !std::isfinite(curve.long_factor_vol)) {
            throw ValidationError("EvolveGabillonCurves: factor volatilities must be finite and >= 0 for " +
                                  curve.product_code + ".");
        }
    }
    for (const GabillonContract& contract : spec.contracts) {
        if (contract.curve_index >= spec.curves.size()) {
            throw ValidationError("EvolveGabillonCurves: contract " + contract.contract_ticker +
                                  " points at a curve that is not in the spec.");
        }
        if (!(contract.anchor_price > 0.0) || !std::isfinite(contract.anchor_price)) {
            throw ValidationError("EvolveGabillonCurves: contract " + contract.contract_ticker +
                                  " must anchor on a positive settle.");
        }
    }
}

}  // namespace

double ShockCorrelationFromCholesky(const quant::CholeskyFactor& factor, const std::size_t i,
                                    const std::size_t j) {
    double dot = 0.0;
    for (std::size_t k = 0; k <= std::min(i, j); ++k) {
        dot += factor.lower[(i * factor.n) + k] * factor.lower[(j * factor.n) + k];
    }
    return dot;
}

void EvolveGabillonCurves(ScenarioBuffer& buffer, const ExposureTimeGrid& time_grid,
                          const GabillonSimulationSpec& spec, IRandomEngine& engine) {
    Validate(buffer, time_grid, spec);

    const std::size_t num_contracts = spec.contracts.size();
    const std::size_t num_shocks = spec.NumShocks();

    for (std::size_t contract = 0; contract < num_contracts; ++contract) {
        const std::span<double> initial = buffer.Slab(contract, 0);
        for (double& price : initial) {
            price = spec.contracts[contract].anchor_price;
        }
    }

    std::vector<double> curve_correlation(spec.curves.size(), 0.0);
    for (std::size_t curve = 0; curve < spec.curves.size(); ++curve) {
        curve_correlation[curve] = ShockCorrelationFromCholesky(spec.cholesky, curve * 2U, (curve * 2U) + 1U);
    }

    StandardNormalGenerator normals(&engine);
    std::vector<double> independent_normals(num_shocks);
    std::vector<double> correlated_shocks(num_shocks);
    std::vector<StepCoefficients> coefficients(num_contracts);

    for (std::size_t step = 1; step < time_grid.NumSteps(); ++step) {
        const double from_years = time_grid.nodes[step - 1].year_fraction;
        const double to_years = time_grid.nodes[step].year_fraction;

        for (std::size_t index = 0; index < num_contracts; ++index) {
            const GabillonContract& contract = spec.contracts[index];
            const GabillonCurveParams& curve = spec.curves[contract.curve_index];
            StepCoefficients& coefficient = coefficients[index];

            // Past settlement the contract no longer exists to be marked, so it holds its
            // last level and the leg pricer drops it off the grid.
            if (contract.settlement_years <= from_years) {
                coefficient.settled = true;
                continue;
            }
            coefficient.settled = false;

            // A contract settling mid-step only diffuses up to its own settlement.
            const double dt = std::min(to_years, contract.settlement_years) - from_years;
            // Loadings are taken at the middle of the step: they move as the contract ages
            // and the midpoint is what makes the discretised variance match the integral
            // of the continuous one to second order.
            const double tenor = contract.settlement_years - (from_years + (0.5 * dt));
            const double decay = std::exp(-curve.mean_reversion * tenor);

            coefficient.short_load = curve.short_factor_vol * decay;
            coefficient.long_load = curve.long_factor_vol * (1.0 - decay);
            const double variance_rate =
                    (coefficient.short_load * coefficient.short_load) +
                    (coefficient.long_load * coefficient.long_load) +
                    (2.0 * curve_correlation[contract.curve_index] * coefficient.short_load *
                     coefficient.long_load);
            coefficient.drift = -0.5 * variance_rate * dt;
            coefficient.sqrt_dt = std::sqrt(dt);
        }

        for (std::size_t path = 0; path < buffer.NumPaths(); ++path) {
            normals.Fill(independent_normals);
            quant::ApplyLowerTriangular(spec.cholesky, independent_normals, correlated_shocks);

            for (std::size_t index = 0; index < num_contracts; ++index) {
                const double previous = buffer.At(index, step - 1, path);
                const StepCoefficients& coefficient = coefficients[index];
                if (coefficient.settled) {
                    buffer.At(index, step, path) = previous;
                    continue;
                }
                const std::size_t short_shock = spec.contracts[index].curve_index * 2U;
                const double shock = (coefficient.short_load * correlated_shocks[short_shock]) +
                                     (coefficient.long_load * correlated_shocks[short_shock + 1U]);
                buffer.At(index, step, path) =
                        previous * std::exp(coefficient.drift + (coefficient.sqrt_dt * shock));
            }
        }
    }
}

}  // namespace numeraire::simulation
