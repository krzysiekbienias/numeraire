#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace numeraire::quant {

/// Curve pillar product used during bootstrap (mirrors `par_curve_point_eod.instrument_type`).
enum class CurvePillarKind : std::uint8_t {
    kDeposit,
    kSwap,
};

/// One quoted pillar before bootstrap. `quoted_rate` is the market par/simple quote \(R_i\)
/// (annualized decimal, e.g. 0.0379 = 3.79%), **not** the bootstrapped zero rate.
struct CurvePillarQuote {
    std::string tenor;
    int tenor_days{0};
    CurvePillarKind kind{CurvePillarKind::kDeposit};
    double quoted_rate{0.0};
};

/// One solved pillar on the continuously compounded zero curve.
struct BootstrappedCurvePoint {
    std::string tenor;
    double time_years{0.0};
    double zero_rate{0.0};
    double discount_factor{0.0};
};

enum class BootstrapStatus : std::uint8_t {
    kOk,
    kInvalidInputs,
    kUnsupportedInstrument,
    kNoConvergence,
};

struct BootstrapResult {
    BootstrapStatus status{BootstrapStatus::kInvalidInputs};
    std::vector<BootstrappedCurvePoint> pillars;
};

/// Act/365 year fraction used for curve pillars (`tenor_days / 365.0`).
[[nodiscard]] constexpr double CurvePillarTimeYears(const int tenor_days) noexcept {
    return static_cast<double>(tenor_days) / 365.0;
}

/// Deposit pillar: simple quote → continuous zero rate.
[[nodiscard]] double DepositZeroRateFromQuote(const double quoted_rate, const double time_years) noexcept;

/// Continuous zero rate → discount factor \(DF(t) = e^{-Z t}\).
[[nodiscard]] double DiscountFactorFromZeroRate(const double zero_rate, const double time_years) noexcept;

/// Build a continuously compounded zero curve from quoted deposit/swap pillars.
///
/// Deposits (short end): \(DF = 1 / (1 + R T)\), \(Z = -\ln(DF) / T\).
/// Swaps (long end): semi-annual par bond with coupon \(c = R/2\); missing intermediate
/// pillars are filled by **linear zero-rate interpolation** in time, with the terminal
/// node solved numerically via bisection on the par-pricing residual.
[[nodiscard]] BootstrapResult BootstrapDiscountCurve(const std::vector<CurvePillarQuote>& quotes);

}  // namespace numeraire::quant
