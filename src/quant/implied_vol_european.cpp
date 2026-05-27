#include <numeraire/quant/implied_vol_european.hpp>

#include <numeraire/quant/black_scholes_vanilla.hpp>

#include <algorithm>
#include <cmath>

namespace numeraire::quant {
namespace {

constexpr double kMinVol = 1.0e-6;
constexpr double kMaxVol = 5.0;
constexpr double kMinTau = 1.0 / 365.0 / 10.0;
constexpr double kPriceTol = 1.0e-8;
constexpr int kMaxNewtonIter = 40;
constexpr int kMaxBisectIter = 80;
constexpr double kIntrinsicSlack = 1.0e-6;

[[nodiscard]] bool InputsValid(const double market_price,
                               const double spot,
                               const double strike,
                               const double time_to_expiry_years) {
    return market_price > 0.0 && spot > 0.0 && strike > 0.0 && time_to_expiry_years >= kMinTau &&
           std::isfinite(market_price) && std::isfinite(spot) && std::isfinite(strike);
}

[[nodiscard]] ImpliedVolResult BisectImpliedVol(const OptionType option_type,
                                                 const double market_price,
                                                 const double spot,
                                                 const double strike,
                                                 const double risk_free_rate,
                                                 const double dividend_yield,
                                                 const double time_to_expiry_years) {
    double lo = kMinVol;
    double hi = kMaxVol;
    double p_lo = EuropeanVanillaPrice(option_type, spot, strike, risk_free_rate, dividend_yield, lo,
                                       time_to_expiry_years);
    double p_hi = EuropeanVanillaPrice(option_type, spot, strike, risk_free_rate, dividend_yield, hi,
                                       time_to_expiry_years);
    if (p_lo > market_price || p_hi < market_price) {
        return {ImpliedVolStatus::kNoConvergence, 0.0};
    }
    for (int i = 0; i < kMaxBisectIter; ++i) {
        const double mid = 0.5 * (lo + hi);
        const double p_mid =
                EuropeanVanillaPrice(option_type, spot, strike, risk_free_rate, dividend_yield, mid, time_to_expiry_years);
        if (std::abs(p_mid - market_price) < kPriceTol) {
            return {ImpliedVolStatus::kOk, mid};
        }
        if (p_mid < market_price) {
            lo = mid;
            p_lo = p_mid;
        } else {
            hi = mid;
            p_hi = p_mid;
        }
    }
    return {ImpliedVolStatus::kNoConvergence, 0.5 * (lo + hi)};
}

}  // namespace

ImpliedVolResult ImpliedVolEuropeanVanilla(const OptionType option_type,
                                           const double market_price,
                                           const double spot,
                                           const double strike,
                                           const double risk_free_rate,
                                           const double dividend_yield,
                                           const double time_to_expiry_years) {
    if (!InputsValid(market_price, spot, strike, time_to_expiry_years)) {
        return {ImpliedVolStatus::kInvalidInputs, 0.0};
    }

    const double intrinsic = EuropeanVanillaIntrinsic(option_type, spot, strike);
    if (market_price < intrinsic - kIntrinsicSlack) {
        return {ImpliedVolStatus::kBelowIntrinsic, 0.0};
    }
    if (std::abs(market_price - intrinsic) <= kIntrinsicSlack) {
        return {ImpliedVolStatus::kOk, 0.0};
    }

    const double p_max =
            EuropeanVanillaPrice(option_type, spot, strike, risk_free_rate, dividend_yield, kMaxVol, time_to_expiry_years);
    if (market_price > p_max + kIntrinsicSlack) {
        return {ImpliedVolStatus::kNoConvergence, 0.0};
    }

    double sigma = 0.2;
    for (int i = 0; i < kMaxNewtonIter; ++i) {
        const double model =
                EuropeanVanillaPrice(option_type, spot, strike, risk_free_rate, dividend_yield, sigma, time_to_expiry_years);
        const double diff = model - market_price;
        if (std::abs(diff) < kPriceTol) {
            return {ImpliedVolStatus::kOk, sigma};
        }
        const double vega =
                EuropeanVanillaVega(option_type, spot, strike, risk_free_rate, dividend_yield, sigma, time_to_expiry_years);
        if (vega < 1.0e-12) {
            break;
        }
        sigma -= diff / vega;
        sigma = std::clamp(sigma, kMinVol, kMaxVol);
    }

    return BisectImpliedVol(option_type, market_price, spot, strike, risk_free_rate, dividend_yield, time_to_expiry_years);
}

}  // namespace numeraire::quant
