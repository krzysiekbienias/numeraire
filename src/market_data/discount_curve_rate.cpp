#include <numeraire/market_data/discount_curve_rate.hpp>

#include <numeraire/quant/discount_curve_bootstrap.hpp>

#include <cmath>

namespace numeraire::market_data {

double RiskFreeRateForTenor(const std::optional<database::DiscountCurveEodRead>& curve,
                            const double flat_fallback_rate,
                            const double time_to_expiry_years) noexcept {
    if (!curve.has_value() || curve->pillars.empty()) {
        return flat_fallback_rate;
    }
    if (time_to_expiry_years <= 0.0) {
        return flat_fallback_rate;
    }
    return quant::InterpolateZeroRateAtTime(curve->pillars, time_to_expiry_years);
}

double RepresentativeRiskFreeRate(const std::optional<database::DiscountCurveEodRead>& curve,
                                  const double flat_fallback_rate) noexcept {
    if (!curve.has_value() || curve->pillars.empty()) {
        return flat_fallback_rate;
    }
    constexpr double kOneYear = 1.0;
    return quant::InterpolateZeroRateAtTime(curve->pillars, kOneYear);
}

}  // namespace numeraire::market_data
