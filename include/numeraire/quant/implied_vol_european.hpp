#pragma once

#include <cstdint>

#include <numeraire/enums/option_type.hpp>

namespace numeraire::quant {

enum class ImpliedVolStatus : std::uint8_t {
    kOk,
    kInvalidInputs,
    kBelowIntrinsic,
    kNoConvergence,
};

struct ImpliedVolResult {
    ImpliedVolStatus status{ImpliedVolStatus::kInvalidInputs};
    double implied_vol{0.0};
};

/// Invert Black–Scholes European vanilla price to absolute \(\sigma\) (not percent).
/// `market_price` is the observed premium per unit underlying (e.g. EOD `close`).
[[nodiscard]] ImpliedVolResult ImpliedVolEuropeanVanilla(OptionType option_type,
                                                         double market_price,
                                                         double spot,
                                                         double strike,
                                                         double risk_free_rate,
                                                         double dividend_yield,
                                                         double time_to_expiry_years);

}  // namespace numeraire::quant
