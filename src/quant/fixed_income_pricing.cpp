#include <numeraire/quant/fixed_income_pricing.hpp>

#include <cmath>
#include <limits>

namespace numeraire::quant {
namespace {

[[nodiscard]] bool IsInvalidCurveTime(const double time_years) noexcept {
    return !std::isfinite(time_years) || time_years < 0.0;
}

[[nodiscard]] double InvalidPv() noexcept {
    return std::numeric_limits<double>::quiet_NaN();
}

}  // namespace

double DiscountFactorAtTime(const std::vector<BootstrappedCurvePoint>& pillars,
                            const double time_years) noexcept {
    if (pillars.empty() || IsInvalidCurveTime(time_years)) {
        return InvalidPv();
    }
    const double zero_rate = InterpolateZeroRateAtTime(pillars, time_years);
    return DiscountFactorFromZeroRate(zero_rate, time_years);
}

double SimpleForwardRateFromDiscountFactors(const double discount_factor_start,
                                            const double discount_factor_end,
                                            const double accrual_time_years) noexcept {
    if (accrual_time_years <= 0.0 || !std::isfinite(accrual_time_years) || !std::isfinite(discount_factor_start) ||
        !std::isfinite(discount_factor_end) || discount_factor_start <= 0.0 || discount_factor_end <= 0.0) {
        return InvalidPv();
    }
    return ((discount_factor_start / discount_factor_end) - 1.0) / accrual_time_years;
}

double ZeroCouponBondPv(const std::vector<BootstrappedCurvePoint>& pillars,
                        const double notional,
                        const double maturity_time_years) noexcept {
    if (!std::isfinite(notional) || pillars.empty() || IsInvalidCurveTime(maturity_time_years)) {
        return InvalidPv();
    }
    const double discount_factor = DiscountFactorAtTime(pillars, maturity_time_years);
    if (!std::isfinite(discount_factor)) {
        return InvalidPv();
    }
    return notional * discount_factor;
}

double FixedCouponBondPv(const std::vector<BootstrappedCurvePoint>& pillars,
                         const std::vector<FixedIncomeCashflow>& cashflows) noexcept {
    if (pillars.empty() || cashflows.empty()) {
        return InvalidPv();
    }

    double pv = 0.0;
    for (const FixedIncomeCashflow& cashflow : cashflows) {
        if (!std::isfinite(cashflow.amount) || IsInvalidCurveTime(cashflow.time_years)) {
            return InvalidPv();
        }
        const double discount_factor = DiscountFactorAtTime(pillars, cashflow.time_years);
        if (!std::isfinite(discount_factor)) {
            return InvalidPv();
        }
        pv += cashflow.amount * discount_factor;
    }
    return pv;
}

double FraNpv(const std::vector<BootstrappedCurvePoint>& pillars,
              const double notional,
              const double fixed_rate,
              const double accrual_start_time_years,
              const double accrual_end_time_years,
              const double payment_time_years) noexcept {
    if (!std::isfinite(notional) || !std::isfinite(fixed_rate) || pillars.empty() ||
        IsInvalidCurveTime(accrual_start_time_years) || IsInvalidCurveTime(accrual_end_time_years) ||
        IsInvalidCurveTime(payment_time_years)) {
        return InvalidPv();
    }

    const double accrual_time_years = accrual_end_time_years - accrual_start_time_years;
    if (accrual_time_years <= 0.0) {
        return InvalidPv();
    }

    const double discount_factor_start = DiscountFactorAtTime(pillars, accrual_start_time_years);
    const double discount_factor_end = DiscountFactorAtTime(pillars, accrual_end_time_years);
    const double discount_factor_payment = DiscountFactorAtTime(pillars, payment_time_years);
    if (!std::isfinite(discount_factor_start) || !std::isfinite(discount_factor_end) ||
        !std::isfinite(discount_factor_payment)) {
        return InvalidPv();
    }

    const double forward_rate =
            SimpleForwardRateFromDiscountFactors(discount_factor_start, discount_factor_end, accrual_time_years);
    if (!std::isfinite(forward_rate)) {
        return InvalidPv();
    }

    return notional * accrual_time_years * discount_factor_payment * (fixed_rate - forward_rate);
}

}  // namespace numeraire::quant
