#include <numeraire/quant/black_scholes_vanilla.hpp>

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

}  // namespace numeraire::quant
