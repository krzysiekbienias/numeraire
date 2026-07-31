#pragma once

#include <numeraire/enums/exercise_style.hpp>
#include <numeraire/enums/option_type.hpp>

#include <cstddef>

namespace numeraire::quant {

/// Cox–Ross–Rubinstein recombining tree for equity vanilla (continuous \(r\), \(q\)).
/// Tree state is a single flat `std::vector` of size `n_steps + 1` (in-place rollback).
/// Amounts are **per one unit of underlying**.
[[nodiscard]] double CoxRossRubinsteinVanillaPrice(OptionType option_type,
                                                   ExerciseStyle exercise,
                                                   double spot,
                                                   double strike,
                                                   double risk_free_rate,
                                                   double dividend_yield,
                                                   double volatility,
                                                   double time_to_expiry_years,
                                                   std::size_t n_steps);

}  // namespace numeraire::quant
