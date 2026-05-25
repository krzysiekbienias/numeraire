#include <cmath>
#include <numbers>
#include <numeraire/core/imarket_data.hpp>
#include <numeraire/core/iproduct.hpp>
#include <numeraire/core/pricing_result.hpp>
#include <numeraire/enums/exercise_style.hpp>
#include <numeraire/enums/option_type.hpp>
#include <numeraire/pricers/analytic_black_scholes_equity_pricer.hpp>
#include <numeraire/products/equity_asset_or_nothing_product.hpp>
#include <numeraire/products/equity_cash_or_nothing_product.hpp>
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

[[nodiscard]] double D1(const double spot,
                        const double strike,
                        const double r,
                        const double q,
                        const double sigma,
                        const double tau) {
    const double srt = sigma * std::sqrt(tau);
    return (std::log(spot / strike) + ((r - q) + 0.5 * sigma * sigma) * tau) / srt;
}

[[nodiscard]] double VanillaIntrinsicNpv(const OptionType kind, const double spot, const double strike) {
    if (kind == OptionType::kCall) {
        return std::max(spot - strike, 0.0);
    }
    return std::max(strike - spot, 0.0);
}

[[nodiscard]] double AssetOrNothingIntrinsicNpv(const OptionType kind, const double spot, const double strike) {
    if (kind == OptionType::kCall) {
        return spot > strike ? spot : 0.0;
    }
    return spot < strike ? spot : 0.0;
}

[[nodiscard]] double CashOrNothingIntrinsicNpv(const OptionType kind, const double spot, const double strike,
                                               const double cash_payout) {
    if (kind == OptionType::kCall) {
        return spot > strike ? cash_payout : 0.0;
    }
    return spot < strike ? cash_payout : 0.0;
}

[[nodiscard]] double DiscountedForwardVanillaIntrinsic(const OptionType kind,
                                                       const double forward,
                                                       const double strike,
                                                       const double discount) {
    if (kind == OptionType::kCall) {
        return discount * std::max(forward - strike, 0.0);
    }
    return discount * std::max(strike - forward, 0.0);
}

[[nodiscard]] double AssetOrNothingZeroVolNpv(const OptionType kind,
                                              const double spot,
                                              const double strike,
                                              const double r,
                                              const double q,
                                              const double tau) {
    const double forward = spot * std::exp((r - q) * tau);
    const double eqt = std::exp(-q * tau);
    if (kind == OptionType::kCall) {
        return forward > strike ? spot * eqt : 0.0;
    }
    return forward < strike ? spot * eqt : 0.0;
}

[[nodiscard]] double CashOrNothingZeroVolNpv(const OptionType kind,
                                             const double spot,
                                             const double strike,
                                             const double cash_payout,
                                             const double r,
                                             const double q,
                                             const double tau) {
    const double forward = spot * std::exp((r - q) * tau);
    const double discount = std::exp(-r * tau);
    if (kind == OptionType::kCall) {
        return forward > strike ? cash_payout * discount : 0.0;
    }
    return forward < strike ? cash_payout * discount : 0.0;
}

[[nodiscard]] double BlackScholesVanillaNpv(const OptionType kind,
                                            const double spot,
                                            const double strike,
                                            const double r,
                                            const double q,
                                            const double sigma,
                                            const double tau) {
    const double d1 = D1(spot, strike, r, q, sigma, tau);
    const double d2 = d1 - sigma * std::sqrt(tau);
    if (kind == OptionType::kCall) {
        return (spot * std::exp(-q * tau) * NormCdf(d1)) - (strike * std::exp(-r * tau) * NormCdf(d2));
    }
    return (strike * std::exp(-r * tau) * NormCdf(-d2)) - (spot * std::exp(-q * tau) * NormCdf(-d1));
}

/// Asset-or-nothing: pays \(S_T\) if ITM. Call: \(S e^{-qT} N(d_1)\); put: \(S e^{-qT} N(-d_1)\).
[[nodiscard]] double AssetOrNothingNpv(const OptionType kind,
                                     const double spot,
                                     const double strike,
                                     const double r,
                                     const double q,
                                     const double sigma,
                                     const double tau) {
    const double d1 = D1(spot, strike, r, q, sigma, tau);
    const double eqt = std::exp(-q * tau);
    if (kind == OptionType::kCall) {
        return spot * eqt * NormCdf(d1);
    }
    return spot * eqt * NormCdf(-d1);
}

/// Cash-or-nothing: pays fixed cash Q if ITM. Call: \(Q e^{-rT} N(d_2)\); put: \(Q e^{-rT} N(-d_2)\).
[[nodiscard]] double CashOrNothingNpv(const OptionType kind,
                                    const double spot,
                                    const double strike,
                                    const double cash_payout,
                                    const double r,
                                    const double q,
                                    const double sigma,
                                    const double tau) {
    const double d1 = D1(spot, strike, r, q, sigma, tau);
    const double d2 = d1 - sigma * std::sqrt(tau);
    const double discount = std::exp(-r * tau);
    if (kind == OptionType::kCall) {
        return cash_payout * discount * NormCdf(d2);
    }
    return cash_payout * discount * NormCdf(-d2);
}

[[nodiscard]] core::PricingGreeks BlackScholesVanillaGreeks(const OptionType kind,
                                                              const double spot,
                                                              const double strike,
                                                              const double r,
                                                              const double q,
                                                              const double sigma,
                                                              const double tau) {
    const double srt = sigma * std::sqrt(tau);
    const double d1 = D1(spot, strike, r, q, sigma, tau);
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

[[nodiscard]] core::PricingResult PriceVanilla(const products::VanillaEquityOptionProduct& vanilla,
                                               const core::IMarketData& market) {
    if (vanilla.Exercise() != ExerciseStyle::kEuropean) {
        throw ValidationError("AnalyticBlackScholesEquityPricer supports European exercise only");
    }

    const double time_to_expiry =
            schedule::Act365FixedYearFraction(market.ValuationDate(), vanilla.ExpiryDate());
    const double spot = market.Spot(vanilla.UnderlyingId());
    const double strike = vanilla.Strike();
    const double r = market.RiskFreeRate();
    const double q = market.DividendYield(vanilla.UnderlyingId());

    core::PricingResult result;

    if (time_to_expiry <= 0.0) {
        result.SetNpv(VanillaIntrinsicNpv(vanilla.OptionKind(), spot, strike));
        return result;
    }

    const double vol = market.ImpliedVolatility(vanilla.UnderlyingId(), strike, time_to_expiry);

    if (vol <= 0.0) {
        const double forward = spot * std::exp((r - q) * time_to_expiry);
        const double discount = std::exp(-r * time_to_expiry);
        result.SetNpv(DiscountedForwardVanillaIntrinsic(vanilla.OptionKind(), forward, strike, discount));
        return result;
    }

    result.SetNpv(BlackScholesVanillaNpv(vanilla.OptionKind(), spot, strike, r, q, vol, time_to_expiry));
    result.SetGreeks(
            BlackScholesVanillaGreeks(vanilla.OptionKind(), spot, strike, r, q, vol, time_to_expiry));
    return result;
}

[[nodiscard]] core::PricingResult PriceAssetOrNothing(const products::EquityAssetOrNothingProduct& aon,
                                                      const core::IMarketData& market) {
    if (aon.Exercise() != ExerciseStyle::kEuropean) {
        throw ValidationError("AnalyticBlackScholesEquityPricer supports European exercise only");
    }

    const double time_to_expiry = schedule::Act365FixedYearFraction(market.ValuationDate(), aon.ExpiryDate());
    const double spot = market.Spot(aon.UnderlyingId());
    const double strike = aon.Strike();
    const double r = market.RiskFreeRate();
    const double q = market.DividendYield(aon.UnderlyingId());

    core::PricingResult result;

    if (time_to_expiry <= 0.0) {
        result.SetNpv(AssetOrNothingIntrinsicNpv(aon.OptionKind(), spot, strike));
        return result;
    }

    const double vol = market.ImpliedVolatility(aon.UnderlyingId(), strike, time_to_expiry);

    if (vol <= 0.0) {
        result.SetNpv(AssetOrNothingZeroVolNpv(aon.OptionKind(), spot, strike, r, q, time_to_expiry));
        return result;
    }

    result.SetNpv(AssetOrNothingNpv(aon.OptionKind(), spot, strike, r, q, vol, time_to_expiry));
    return result;
}

[[nodiscard]] core::PricingResult PriceCashOrNothing(const products::EquityCashOrNothingProduct& con,
                                                     const core::IMarketData& market) {
    if (con.Exercise() != ExerciseStyle::kEuropean) {
        throw ValidationError("AnalyticBlackScholesEquityPricer supports European exercise only");
    }

    const double time_to_expiry = schedule::Act365FixedYearFraction(market.ValuationDate(), con.ExpiryDate());
    const double spot = market.Spot(con.UnderlyingId());
    const double strike = con.Strike();
    const double cash_payout = con.CashPayoutPerShare();
    const double r = market.RiskFreeRate();
    const double q = market.DividendYield(con.UnderlyingId());

    core::PricingResult result;

    if (time_to_expiry <= 0.0) {
        result.SetNpv(CashOrNothingIntrinsicNpv(con.OptionKind(), spot, strike, cash_payout));
        return result;
    }

    const double vol = market.ImpliedVolatility(con.UnderlyingId(), strike, time_to_expiry);

    if (vol <= 0.0) {
        result.SetNpv(
                CashOrNothingZeroVolNpv(con.OptionKind(), spot, strike, cash_payout, r, q, time_to_expiry));
        return result;
    }

    result.SetNpv(CashOrNothingNpv(con.OptionKind(), spot, strike, cash_payout, r, q, vol, time_to_expiry));
    return result;
}

}  // namespace

numeraire::PricingEngineType AnalyticBlackScholesEquityPricer::EngineKind() const {
    return numeraire::PricingEngineType::kAnalytic;
}

core::PricingResult AnalyticBlackScholesEquityPricer::Price(const core::IProduct& product,
                                                            const core::IMarketData& market) const {
    if (const auto* vanilla = dynamic_cast<const products::VanillaEquityOptionProduct*>(&product)) {
        return PriceVanilla(*vanilla, market);
    }
    if (const auto* aon = dynamic_cast<const products::EquityAssetOrNothingProduct*>(&product)) {
        return PriceAssetOrNothing(*aon, market);
    }
    if (const auto* con = dynamic_cast<const products::EquityCashOrNothingProduct*>(&product)) {
        return PriceCashOrNothing(*con, market);
    }
    throw ValidationError(
            "AnalyticBlackScholesEquityPricer requires VanillaEquityOptionProduct, "
            "EquityAssetOrNothingProduct, or EquityCashOrNothingProduct");
}

}  // namespace numeraire::pricers
