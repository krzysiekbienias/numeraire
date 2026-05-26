#include <cmath>
#include <numeraire/core/imarket_data.hpp>
#include <numeraire/core/pricing_result.hpp>
#include <numeraire/pricers/analytic_forward_pricer.hpp>
#include <numeraire/products/equity_forward_product.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/utils/exception.hpp>

namespace numeraire::pricers {

namespace {

[[nodiscard]] double ForwardIntrinsicNpv(const double spot, const double forward_price) {
    return spot - forward_price;
}

[[nodiscard]] double ForwardNpv(const double spot,
                                const double forward_price,
                                const double r,
                                const double q,
                                const double tau) {
    return (spot * std::exp(-q * tau)) - (forward_price * std::exp(-r * tau));
}

[[nodiscard]] core::PricingResult PriceEquityForward(const products::EquityForwardProduct& forward,
                                                     const core::IMarketData& market) {
    const double time_to_expiry =
            schedule::Act365FixedYearFraction(market.ValuationDate(), forward.ExpiryDate());
    const double spot = market.Spot(forward.UnderlyingId());
    const double forward_price = forward.Strike();
    const double r = market.RiskFreeRate();
    const double q = market.DividendYield(forward.UnderlyingId());

    core::PricingResult result;
    if (time_to_expiry <= 0.0) {
        result.SetNpv(ForwardIntrinsicNpv(spot, forward_price));
        return result;
    }

    result.SetNpv(ForwardNpv(spot, forward_price, r, q, time_to_expiry));
    return result;
}

}  // namespace

numeraire::PricingEngineType AnalyticForwardPricer::EngineKind() const {
    return numeraire::PricingEngineType::kAnalytic;
}

core::PricingResult AnalyticForwardPricer::Price(const core::IProduct& product,
                                                 const core::IMarketData& market) const {
    if (const auto* forward = dynamic_cast<const products::EquityForwardProduct*>(&product)) {
        return PriceEquityForward(*forward, market);
    }
    throw ValidationError("AnalyticForwardPricer requires EquityForwardProduct");
}

}  // namespace numeraire::pricers
