#include <numeraire/quant/black_scholes_vanilla.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace numeraire::quant {
namespace {

[[nodiscard]] double NormCdf(const double x) {
    return 0.5 * (1.0 + std::erf(x / std::numbers::sqrt2));
}

[[nodiscard]] double NormPdf(const double x) {
    return std::exp(-0.5 * x * x) / std::sqrt(2.0 * std::numbers::pi);
}

[[nodiscard]] double D1(const double spot,
                        const double strike,
                        const double r,
                        const double q,
                        const double sigma,
                        const double tau) {
    const double srt = sigma * std::sqrt(tau);
    return (std::log(spot / strike) + ((r - q) + 0.5 * sigma * sigma) * tau) / srt;
}

/// Deterministic \(\sigma = 0\) limit: the spot diffuses to its forward with certainty.
[[nodiscard]] double ForwardPrice(const double spot,
                                  const double r,
                                  const double q,
                                  const double tau) {
    return spot * std::exp((r - q) * tau);
}

}  // namespace

double EuropeanVanillaIntrinsic(const OptionType option_type, const double spot, const double strike) {
    if (option_type == OptionType::kCall) {
        return std::max(spot - strike, 0.0);
    }
    return std::max(strike - spot, 0.0);
}

double EuropeanVanillaPrice(const OptionType option_type,
                            const double spot,
                            const double strike,
                            const double risk_free_rate,
                            const double dividend_yield,
                            const double volatility,
                            const double time_to_expiry_years) {
    if (time_to_expiry_years <= 0.0) {
        return EuropeanVanillaIntrinsic(option_type, spot, strike);
    }
    if (volatility <= 0.0) {
        const double forward = ForwardPrice(spot, risk_free_rate, dividend_yield, time_to_expiry_years);
        const double discount = std::exp(-risk_free_rate * time_to_expiry_years);
        return discount * EuropeanVanillaIntrinsic(option_type, forward, strike);
    }
    const double d1 = D1(spot, strike, risk_free_rate, dividend_yield, volatility, time_to_expiry_years);
    const double d2 = d1 - volatility * std::sqrt(time_to_expiry_years);
    if (option_type == OptionType::kCall) {
        return (spot * std::exp(-dividend_yield * time_to_expiry_years) * NormCdf(d1)) -
               (strike * std::exp(-risk_free_rate * time_to_expiry_years) * NormCdf(d2));
    }
    return (strike * std::exp(-risk_free_rate * time_to_expiry_years) * NormCdf(-d2)) -
           (spot * std::exp(-dividend_yield * time_to_expiry_years) * NormCdf(-d1));
}

double EuropeanVanillaVega(const OptionType option_type,
                           const double spot,
                           const double strike,
                           const double risk_free_rate,
                           const double dividend_yield,
                           const double volatility,
                           const double time_to_expiry_years) {
    static_cast<void>(option_type);
    if (time_to_expiry_years <= 0.0 || volatility <= 0.0) {
        return 0.0;
    }
    const double d1 = D1(spot, strike, risk_free_rate, dividend_yield, volatility, time_to_expiry_years);
    return spot * std::exp(-dividend_yield * time_to_expiry_years) * NormPdf(d1) * std::sqrt(time_to_expiry_years);
}

EuropeanVanillaGreeks EuropeanVanillaAllGreeks(const OptionType option_type,
                                               const double spot,
                                               const double strike,
                                               const double risk_free_rate,
                                               const double dividend_yield,
                                               const double volatility,
                                               const double time_to_expiry_years) {
    const double srt = volatility * std::sqrt(time_to_expiry_years);
    const double d1 = D1(spot, strike, risk_free_rate, dividend_yield, volatility, time_to_expiry_years);
    const double d2 = d1 - srt;
    const double nd1 = NormPdf(d1);
    const double eqt = std::exp(-dividend_yield * time_to_expiry_years);
    const double ert = std::exp(-risk_free_rate * time_to_expiry_years);

    EuropeanVanillaGreeks greeks;
    greeks.gamma = (eqt * nd1) / (spot * srt);
    greeks.vega = spot * eqt * nd1 * std::sqrt(time_to_expiry_years);

    if (option_type == OptionType::kCall) {
        greeks.delta = eqt * NormCdf(d1);
        greeks.theta = (-(spot * nd1 * volatility * eqt) / (2.0 * std::sqrt(time_to_expiry_years))) -
                       (risk_free_rate * strike * ert * NormCdf(d2)) +
                       (dividend_yield * spot * eqt * NormCdf(d1));
        greeks.rho = strike * time_to_expiry_years * ert * NormCdf(d2);
    } else {
        greeks.delta = -eqt * NormCdf(-d1);
        greeks.theta = (-(spot * nd1 * volatility * eqt) / (2.0 * std::sqrt(time_to_expiry_years))) +
                       (risk_free_rate * strike * ert * NormCdf(-d2)) -
                       (dividend_yield * spot * eqt * NormCdf(-d1));
        greeks.rho = -strike * time_to_expiry_years * ert * NormCdf(-d2);
    }

    return greeks;
}

double AssetOrNothingIntrinsic(const OptionType option_type, const double spot, const double strike) {
    if (option_type == OptionType::kCall) {
        return spot > strike ? spot : 0.0;
    }
    return spot < strike ? spot : 0.0;
}

double AssetOrNothingPrice(const OptionType option_type,
                           const double spot,
                           const double strike,
                           const double risk_free_rate,
                           const double dividend_yield,
                           const double volatility,
                           const double time_to_expiry_years) {
    if (time_to_expiry_years <= 0.0) {
        return AssetOrNothingIntrinsic(option_type, spot, strike);
    }
    const double eqt = std::exp(-dividend_yield * time_to_expiry_years);
    if (volatility <= 0.0) {
        const double forward = ForwardPrice(spot, risk_free_rate, dividend_yield, time_to_expiry_years);
        const bool in_the_money =
                option_type == OptionType::kCall ? forward > strike : forward < strike;
        return in_the_money ? spot * eqt : 0.0;
    }
    const double d1 = D1(spot, strike, risk_free_rate, dividend_yield, volatility, time_to_expiry_years);
    if (option_type == OptionType::kCall) {
        return spot * eqt * NormCdf(d1);
    }
    return spot * eqt * NormCdf(-d1);
}

double CashOrNothingIntrinsic(const OptionType option_type,
                              const double spot,
                              const double strike,
                              const double cash_payout) {
    if (option_type == OptionType::kCall) {
        return spot > strike ? cash_payout : 0.0;
    }
    return spot < strike ? cash_payout : 0.0;
}

double CashOrNothingPrice(const OptionType option_type,
                          const double spot,
                          const double strike,
                          const double cash_payout,
                          const double risk_free_rate,
                          const double dividend_yield,
                          const double volatility,
                          const double time_to_expiry_years) {
    if (time_to_expiry_years <= 0.0) {
        return CashOrNothingIntrinsic(option_type, spot, strike, cash_payout);
    }
    const double discount = std::exp(-risk_free_rate * time_to_expiry_years);
    if (volatility <= 0.0) {
        const double forward = ForwardPrice(spot, risk_free_rate, dividend_yield, time_to_expiry_years);
        const bool in_the_money =
                option_type == OptionType::kCall ? forward > strike : forward < strike;
        return in_the_money ? cash_payout * discount : 0.0;
    }
    const double d1 = D1(spot, strike, risk_free_rate, dividend_yield, volatility, time_to_expiry_years);
    const double d2 = d1 - (volatility * std::sqrt(time_to_expiry_years));
    if (option_type == OptionType::kCall) {
        return cash_payout * discount * NormCdf(d2);
    }
    return cash_payout * discount * NormCdf(-d2);
}

}  // namespace numeraire::quant
