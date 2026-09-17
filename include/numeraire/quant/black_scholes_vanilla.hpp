#pragma once

#include <numeraire/enums/option_type.hpp>

namespace numeraire::quant {

/// European vanilla on equity/index with continuous \f$r\f$, \f$q\f$, constant \f$\sigma\f$.
/// All amounts are **per one unit of underlying** (one share or one index point).
///
/// Degenerate inputs are folded in so callers do not repeat the branches:
/// \f$T \le 0\f$ returns intrinsic on spot, \f$\sigma \le 0\f$ returns the deterministic
/// forward limit \f$e^{-rT}\max(F-K, 0)\f$ with \f$F = S e^{(r-q)T}\f$.
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

/// First-order sensitivities per one unit of underlying, w.r.t. spot \f$S\f$, **absolute**
/// volatility \f$\sigma\f$, and rate \f$r\f$ on the same \f$T\f$ as the price. `theta` is decay
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

/// Undefined for \f$T \le 0\f$ or \f$\sigma \le 0\f$; callers must skip those cases (there is
/// no meaningful sensitivity once the payoff is deterministic).
[[nodiscard]] EuropeanVanillaGreeks EuropeanVanillaAllGreeks(OptionType option_type,
                                                             double spot,
                                                             double strike,
                                                             double risk_free_rate,
                                                             double dividend_yield,
                                                             double volatility,
                                                             double time_to_expiry_years);

/// Asset-or-nothing: pays \f$S_T\f$ if ITM. Call: \f$S e^{-qT} N(d_1)\f$; put: \f$S e^{-qT} N(-d_1)\f$.
/// Same degenerate handling as `EuropeanVanillaPrice`.
[[nodiscard]] double AssetOrNothingPrice(OptionType option_type,
                                         double spot,
                                         double strike,
                                         double risk_free_rate,
                                         double dividend_yield,
                                         double volatility,
                                         double time_to_expiry_years);

/// Value at expiry: spot when ITM (call \f$S > K\f$, put \f$S < K\f$), else zero.
[[nodiscard]] double AssetOrNothingIntrinsic(OptionType option_type, double spot, double strike);

/// Cash-or-nothing: pays `cash_payout` if ITM. Call: \f$Q e^{-rT} N(d_2)\f$; put: \f$Q e^{-rT} N(-d_2)\f$.
/// Same degenerate handling as `EuropeanVanillaPrice`.
[[nodiscard]] double CashOrNothingPrice(OptionType option_type,
                                        double spot,
                                        double strike,
                                        double cash_payout,
                                        double risk_free_rate,
                                        double dividend_yield,
                                        double volatility,
                                        double time_to_expiry_years);

/// Value at expiry: `cash_payout` when ITM (call \f$S > K\f$, put \f$S < K\f$), else zero.
[[nodiscard]] double CashOrNothingIntrinsic(OptionType option_type,
                                            double spot,
                                            double strike,
                                            double cash_payout);

}  // namespace numeraire::quant
