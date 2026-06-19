#pragma once

#include <numeraire/quant/cholesky.hpp>

#include <cstddef>
#include <vector>

namespace numeraire::simulation {

/// Constant-coefficient single-underlying GBM inputs for one simulation batch.
struct SingleFactorGbmSpec {
    double spot{0.0};
    double risk_free_rate{0.0};
    double dividend_yield{0.0};
    double volatility{0.0};
};

/// Constant-coefficient multi-underlying GBM inputs for one simulation batch.
///
/// Per-factor `spots`, `volatilities`, `risk_free_rates`, and `dividend_yields` share
/// the same ordering. `cholesky` maps independent standard normals to correlated
/// diffusion shocks (`eps = L * Z`) at each step.
struct MultiFactorGbmSpec {
    std::vector<double> spots;
    std::vector<double> risk_free_rates;
    std::vector<double> dividend_yields;
    std::vector<double> volatilities;
    quant::CholeskyFactor cholesky;

    [[nodiscard]] std::size_t NumFactors() const noexcept { return spots.size(); }
};

struct HistoricalCalibrationResult;

/// Build a multifactor GBM spec from a calibrated snapshot. Flat `risk_free_rate` and
/// `dividend_yield` are replicated across all factors (tenor-dependent curve later).
[[nodiscard]] MultiFactorGbmSpec BuildMultiFactorGbmSpec(const HistoricalCalibrationResult& calibration,
                                                         double risk_free_rate,
                                                         double dividend_yield = 0.0);

}  // namespace numeraire::simulation
