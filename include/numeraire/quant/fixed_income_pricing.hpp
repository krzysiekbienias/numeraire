#pragma once

#include <numeraire/quant/discount_curve_bootstrap.hpp>

#include <vector>

namespace numeraire::quant {

/// One deterministic cashflow on the discount curve time axis (Act/365 year fraction).
struct FixedIncomeCashflow {
    double time_years{0.0};
    double amount{0.0};
};

/// Discount factor \(DF(t)\) from bootstrapped pillars: linear \(Z\) interpolation, then \(e^{-Zt}\).
[[nodiscard]] double DiscountFactorAtTime(const std::vector<BootstrappedCurvePoint>& pillars,
                                          double time_years) noexcept;

/// Simple forward rate from discount factors over accrual \(\tau\):
/// \(F = \bigl(DF(t_{\mathrm{start}}) / DF(t_{\mathrm{end}}) - 1\bigr) / \tau\).
[[nodiscard]] double SimpleForwardRateFromDiscountFactors(double discount_factor_start,
                                                          double discount_factor_end,
                                                          double accrual_time_years) noexcept;

/// Present value of a zero-coupon bond: \(\mathrm{NPV} = N \cdot DF(T)\).
[[nodiscard]] double ZeroCouponBondPv(const std::vector<BootstrappedCurvePoint>& pillars,
                                    double notional,
                                    double maturity_time_years) noexcept;

/// Present value of deterministic cashflows: \(\mathrm{NPV} = \sum_i c_i \cdot DF(t_i)\).
[[nodiscard]] double FixedCouponBondPv(const std::vector<BootstrappedCurvePoint>& pillars,
                                     const std::vector<FixedIncomeCashflow>& cashflows) noexcept;

/// FRA NPV to the party that **receives fixed** and pays floating (standard FRA buyer):
/// \(\mathrm{NPV} = N \cdot \tau \cdot DF(t_{\mathrm{pay}}) \cdot (K - F)\).
[[nodiscard]] double FraNpv(const std::vector<BootstrappedCurvePoint>& pillars,
                          double notional,
                          double fixed_rate,
                          double accrual_start_time_years,
                          double accrual_end_time_years,
                          double payment_time_years) noexcept;

}  // namespace numeraire::quant
