#include <cmath>
#include <numbers>
#include <numeraire/core/imarket_data.hpp>
#include <numeraire/core/iproduct.hpp>
#include <numeraire/core/pricing_result.hpp>
#include <numeraire/enums/exercise_style.hpp>
#include <numeraire/enums/option_type.hpp>
#include <numeraire/pricers/analytic_black_scholes_equity_pricer.hpp>
#include <numeraire/products/vanilla_equity_option_product.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/utils/exception.hpp>

namespace numeraire::pricers {

namespace {
using std::sqrt;

constexpr double kPi = std::numbers::pi;

[[nodiscard]] double NormCdf(const double x) {
    return 0.5 * (1.0 + std::erf(x / std::numbers::sqrt2));
}

[[nodiscard]] double NormPdf(const double x) {
    return std::exp(-0.5 * x * x) / std::sqrt(2.0 * kPi);
}

[[nodiscard]] double IntrinsicNpv(const OptionType kind, const double spot, const double strike) {
    if (kind == OptionType::kCall) {
        return std::max(spot - strike, 0.0);
    }
    return std::max(strike - spot, 0.0);
}

[[nodiscard]] double DiscountedForwardIntrinsic(const OptionType kind,
                                                const double forward,
                                                const double strike,
                                                const double discount) {
    if (kind == OptionType::kCall) {
        return discount * std::max(forward - strike, 0.0);
    }
    return discount * std::max(strike - forward, 0.0);
}

[[nodiscard]] double BlackScholesNpv(const OptionType kind,
                                     const double spot,
                                     const double strike,
                                     const double r,
                                     const double q,
                                     const double sigma,
                                     const double tau) {
    const double srt = sigma * std::sqrt(tau);
    const double d1 = (std::log(spot / strike) + ((r - q) + 0.5 * sigma * sigma) * tau) / srt;
    const double d2 = d1 - srt;
    if (kind == OptionType::kCall) {
        return (spot * std::exp(-q * tau) * NormCdf(d1)) - (strike * std::exp(-r * tau) * NormCdf(d2));
    }
    return (strike * std::exp(-r * tau) * NormCdf(-d2)) - (spot * std::exp(-q * tau) * NormCdf(-d1));
}

[[nodiscard]] core::PricingGreeks BlackScholesGreeks(const OptionType kind,
                                                     const double spot,
                                                     const double strike,
                                                     const double r,
                                                     const double q,
                                                     const double sigma,
                                                     const double tau) {
    const double srt = sigma * std::sqrt(tau);
    const double d1 = (std::log(spot / strike) + ((r - q) + 0.5 * sigma * sigma) * tau) / srt;
    const double d2 = d1 - srt;
    const double nd1 = NormPdf(d1);
    const double eqt = std::exp(-q * tau);
    const double ert = std::exp(-r * tau);

    core::PricingGreeks greeks;
    const double gamma = (eqt * nd1) / (spot * srt);
    greeks.gamma = gamma;
    greeks.vega = spot * eqt * nd1 * std::sqrt(tau);

    if (kind == OptionType::kCall) {
        greeks.delta = eqt * NormCdf(d1);
        greeks.theta = (-(spot * nd1 * sigma * eqt) / (2.0 * std::sqrt(tau))) - (r * strike * ert * NormCdf(d2)) +
                       (q * spot * eqt * NormCdf(d1));
        greeks.rho = strike * tau * ert * NormCdf(d2);
    } else {
        greeks.delta = -eqt * NormCdf(-d1);
        greeks.theta = (-(spot * nd1 * sigma * eqt) / (2.0 * std::sqrt(tau))) + (r * strike * ert * NormCdf(-d2)) -
                       (q * spot * eqt * NormCdf(-d1));
        greeks.rho = -strike * tau * ert * NormCdf(-d2);
    }

    return greeks;
}

}  // namespace

numeraire::PricingEngineType AnalyticBlackScholesEquityPricer::EngineKind() const {
    return numeraire::PricingEngineType::kAnalytic;
}

core::PricingResult AnalyticBlackScholesEquityPricer::Price(const core::IProduct& product,
                                                            const core::IMarketData& market) const {
    const auto* vanilla = dynamic_cast<const products::VanillaEquityOptionProduct*>(&product);
    if (vanilla == nullptr) {
        throw ValidationError("AnalyticBlackScholesEquityPricer requires products::VanillaEquityOptionProduct");
    }
    if (vanilla->Exercise() != ExerciseStyle::kEuropean) {
        throw ValidationError("AnalyticBlackScholesEquityPricer supports European exercise only");
    }
    const double time_to_expiry = schedule::Act365FixedYearFraction(market.ValuationDate(), vanilla->ExpiryDate());

    const double spot = market.Spot(vanilla->UnderlyingId());
    const double strike = vanilla->Strike();
    const double r = market.RiskFreeRate();
    const double q = market.DividendYield(vanilla->UnderlyingId());

    core::PricingResult result;

    if (time_to_expiry <= 0.0) {
        result.SetNpv(IntrinsicNpv(vanilla->OptionKind(), spot, strike));
        return result;
    }

    const double vol = market.ImpliedVolatility(vanilla->UnderlyingId(), strike, time_to_expiry);

    if (vol <= 0.0) {
        const double forward = spot * std::exp((r - q) * time_to_expiry);
        const double discount = std::exp(-r * time_to_expiry);
        result.SetNpv(DiscountedForwardIntrinsic(vanilla->OptionKind(), forward, strike, discount));
        return result;
    }

    result.SetNpv(BlackScholesNpv(vanilla->OptionKind(), spot, strike, r, q, vol, time_to_expiry));
    result.SetGreeks(BlackScholesGreeks(vanilla->OptionKind(), spot, strike, r, q, vol, time_to_expiry));

    return result;
}

}  // namespace numeraire::pricers
