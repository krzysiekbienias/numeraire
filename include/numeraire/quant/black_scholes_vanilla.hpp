#pragma once

#include <numeraire/enums/option_type.hpp>

namespace numeraire::quant {

/// European vanilla on equity/index with continuous \(r\), \(q\), constant \(\sigma\).
/// All amounts are **per one unit of underlying** (one share or one index point).
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

}  // namespace numeraire::quant
