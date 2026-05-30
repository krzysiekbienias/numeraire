#pragma once

#include <numeraire/database/discount_curve_eod_read.hpp>

namespace numeraire::market_data {

/// Continuous zero rate for pricing: linear interpolation on bootstrapped pillars.
[[nodiscard]] double RiskFreeRateForTenor(const std::optional<database::DiscountCurveEodRead>& curve,
                                          const double flat_fallback_rate,
                                          const double time_to_expiry_years) noexcept;

/// Representative flat rate for logging / MTM header when a curve is loaded (1Y pillar if present).
[[nodiscard]] double RepresentativeRiskFreeRate(const std::optional<database::DiscountCurveEodRead>& curve,
                                                const double flat_fallback_rate) noexcept;

}  // namespace numeraire::market_data
