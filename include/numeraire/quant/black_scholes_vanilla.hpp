#pragma once

#include <numeraire/enums/option_type.hpp>

namespace numeraire::quant {

/// European vanilla on equity/index with continuous \(r\), \(q\), constant \(\sigma\).
/// All amounts are **per one unit of underlying** (one share or one index point).
///
/// Degenerate inputs are folded in so callers do not repeat the branches:
/// \(T \le 0\) returns intrinsic on spot, \(\sigma \le 0\) returns the deterministic
/// forward limit \(e^{-rT}\max(F-K, 0)\) with \(F = S e^{(r-q)T}\).
[[nodiscard]] double EuropeanVanillaPrice(OptionType option_type,
                                          double spot,
                                          double strike,
                                          double risk_free_rate,
                                          double dividend_yield,
                                          double volatility,
                                          double time_to_expiry_years);

[[nodiscard]] double EuropeanVanillaVega(OptionType option_type,
                                         double spot,
                                         double strike,
                                         double risk_free_rate,
                                         double dividend_yield,
                                         double volatility,
                                         double time_to_expiry_years);

[[nodiscard]] double EuropeanVanillaIntrinsic(OptionType option_type, double spot, double strike);

/// First-order sensitivities per one unit of underlying, w.r.t. spot \(S\), **absolute**
/// volatility \(\sigma\), and rate \(r\) on the same \(T\) as the price. `theta` is decay
/// **per calendar year**, not per day.
///
/// Owned by `quant` rather than reusing `core::PricingGreeks` so this module stays a leaf
/// (enums only); adapters map it onto their own result type.
struct EuropeanVanillaGreeks {
    double delta{0.0};
    double gamma{0.0};
    double vega{0.0};
    double theta{0.0};
    double rho{0.0};
};

/// Undefined for \(T \le 0\) or \(\sigma \le 0\); callers must skip those cases (there is
/// no meaningful sensitivity once the payoff is deterministic).
[[nodiscard]] EuropeanVanillaGreeks EuropeanVanillaAllGreeks(OptionType option_type,
                                                             double spot,
                                                             double strike,
                                                             double risk_free_rate,
                                                             double dividend_yield,
                                                             double volatility,
                                                             double time_to_expiry_years);

/// Asset-or-nothing: pays \(S_T\) if ITM. Call: \(S e^{-qT} N(d_1)\); put: \(S e^{-qT} N(-d_1)\).
/// Same degenerate handling as `EuropeanVanillaPrice`.
[[nodiscard]] double AssetOrNothingPrice(OptionType option_type,
                                         double spot,
                                         double strike,
                                         double risk_free_rate,
                                         double dividend_yield,
                                         double volatility,
                                         double time_to_expiry_years);

/// Value at expiry: spot when ITM (call \(S > K\), put \(S < K\)), else zero.
[[nodiscard]] double AssetOrNothingIntrinsic(OptionType option_type, double spot, double strike);

/// Cash-or-nothing: pays `cash_payout` if ITM. Call: \(Q e^{-rT} N(d_2)\); put: \(Q e^{-rT} N(-d_2)\).
/// Same degenerate handling as `EuropeanVanillaPrice`.
[[nodiscard]] double CashOrNothingPrice(OptionType option_type,
                                        double spot,
                                        double strike,
                                        double cash_payout,
                                        double risk_free_rate,
                                        double dividend_yield,
                                        double volatility,
                                        double time_to_expiry_years);

/// Value at expiry: `cash_payout` when ITM (call \(S > K\), put \(S < K\)), else zero.
[[nodiscard]] double CashOrNothingIntrinsic(OptionType option_type,
                                            double spot,
                                            double strike,
                                            double cash_payout);

}  // namespace numeraire::quant
