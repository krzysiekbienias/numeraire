#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace numeraire::quant {

/// One observed settle on the curve being fitted.
struct ForwardCurvePoint {
    /// Act/365 years from the as-of date to the contract's settlement.
    double time_to_settlement_years{0.0};
    /// Where the settlement date falls inside its calendar year, in `[0, 1)`.
    /// Seasonality repeats in this coordinate and never in time to settlement.
    double year_phase{0.0};
    double price{0.0};
};

enum class GabillonFitStatus : std::uint8_t {
    kOk,
    kInvalidInputs,
    kSingularSystem,
};

/// Deterministic seasonal shape as Fourier coefficients over the calendar year:
/// \(\mathrm{seas}(\phi) = \sum_i a_i \cos(2\pi i \phi) + b_i \sin(2\pi i \phi)\).
/// Mean-zero across a full year by construction, so it never competes with the curve level.
struct SeasonalShape {
    std::vector<double> cos_coeffs;
    std::vector<double> sin_coeffs;
};

/// Two-factor Gabillon decomposition of one futures curve.
///
/// \(\ln F(0,T) = e^{-k\tau}\ln S + (1-e^{-k\tau})\ln L + \mathrm{seas}(\phi(T))\)
///
/// The short factor pins the front of the curve, the long factor its asymptote, and the
/// seasonal term carries whatever repeats every calendar year. Separating them is the
/// whole point: a December gas contract has to stay a December contract as it ages,
/// instead of sliding down today's curve slope the way a constant-maturity view makes it.
///
/// The two levels are effective rather than structural. Gabillon's deterministic
/// convexity term depends on \(\tau\) alone, so the fit absorbs it into the same two
/// degrees of freedom — harmless when the purpose is to separate seasonality from curve
/// shape, but it means `short_factor_level` is not literally today's spot.
struct GabillonCurveFit {
    GabillonFitStatus status{GabillonFitStatus::kInvalidInputs};
    double mean_reversion{0.0};
    double short_factor_level{0.0};
    double long_factor_level{0.0};
    SeasonalShape seasonal;
    /// Residual standard deviation in log price.
    double log_rmse{0.0};
    std::size_t num_points{0};
};

/// Seasonal adjustment in log space at `year_phase`.
[[nodiscard]] double SeasonalValue(const SeasonalShape& shape, double year_phase);

/// Model log price for one maturity / phase under a completed fit.
[[nodiscard]] double GabillonCurveLogPrice(const GabillonCurveFit& fit,
                                           double time_to_settlement_years,
                                           double year_phase);

/// Fit with `mean_reversion` held fixed. With \(k\) known the model is linear in the
/// remaining unknowns, so this is one small normal-equation solve.
[[nodiscard]] GabillonCurveFit FitGabillonCurveGivenMeanReversion(
        const std::vector<ForwardCurvePoint>& curve, double mean_reversion, int num_harmonics);

/// Fit including `mean_reversion`, by scanning it and keeping the best linear solve.
/// Cheap enough to brute force: each trial is a 6x6 system for the usual two harmonics.
[[nodiscard]] GabillonCurveFit FitGabillonCurve(const std::vector<ForwardCurvePoint>& curve,
                                                int num_harmonics = 2,
                                                double min_mean_reversion = 0.05,
                                                double max_mean_reversion = 20.0);

/// Instantaneous volatility of \(\ln F\) for a contract \(\tau\) years from settlement:
/// \(\sigma^2 = \sigma_S^2 e^{-2k\tau} + \sigma_L^2 (1-e^{-k\tau})^2
///            + 2\rho\sigma_S\sigma_L e^{-k\tau}(1-e^{-k\tau})\).
///
/// This is the Samuelson effect in closed form — the front of the curve is driven by the
/// short factor, the deferred end by the long factor, and \(k\) sets how fast one hands
/// over to the other.
[[nodiscard]] double GabillonForwardVolatility(double time_to_settlement_years,
                                               double mean_reversion,
                                               double short_factor_vol,
                                               double long_factor_vol,
                                               double factor_correlation) noexcept;

/// Volatility-side parameters recovered from an observed term structure of vols.
struct GabillonVolFit {
    GabillonFitStatus status{GabillonFitStatus::kInvalidInputs};
    double mean_reversion{0.0};
    double short_factor_vol{0.0};
    double long_factor_vol{0.0};
    double factor_correlation{0.0};
    double rmse{0.0};
};

/// Recover \(k, \sigma_S, \sigma_L, \rho\) from pillar volatilities observed at
/// `time_to_settlement_years`, by least squares on `GabillonForwardVolatility`.
///
/// Passing the mean reversion from the curve fit is usually wrong: the curve pins how
/// fast *levels* decay towards the long end, the vols pin how fast *risk* does, and the
/// two only coincide in a perfectly specified model. Fitting them separately and
/// comparing is a useful diagnostic, so `mean_reversion` is solved for here too.
[[nodiscard]] GabillonVolFit FitGabillonVolatilities(const std::vector<double>& time_to_settlement_years,
                                                     const std::vector<double>& volatilities,
                                                     double min_mean_reversion = 0.05,
                                                     double max_mean_reversion = 20.0);

}  // namespace numeraire::quant
