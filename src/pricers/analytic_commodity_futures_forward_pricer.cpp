#include <numeraire/pricers/analytic_commodity_futures_forward_pricer.hpp>

#include <cmath>
#include <numeraire/core/imarket_data.hpp>
#include <numeraire/core/pricing_result.hpp>
#include <numeraire/products/commodity_futures_forward_product.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/utils/exception.hpp>

namespace numeraire::pricers {

namespace {

[[nodiscard]] core::PricingResult PriceCommodityFuturesForward(
        const products::CommodityFuturesForwardProduct& fwd, const core::IMarketData& market) {
    const double time_to_expiry =
            schedule::Act365FixedYearFraction(market.ValuationDate(), fwd.ExpiryDate());
    const double futures_price = market.Quote(fwd.UnderlyingId());
    const double k = fwd.Strike();
    const double r = market.RiskFreeRateForTenor(time_to_expiry > 0.0 ? time_to_expiry : 0.0);

    core::PricingResult result;
    core::PricingGreeks greeks;
    greeks.gamma = 0.0;
    greeks.vega = 0.0;
    greeks.theta = 0.0;
    greeks.rho = 0.0;

    if (time_to_expiry <= 0.0) {
        result.SetNpv(futures_price - k);
        greeks.delta = 1.0;
        result.SetGreeks(greeks);
        return result;
    }

    const double df = std::exp(-r * time_to_expiry);
    result.SetNpv(df * (futures_price - k));
    greeks.delta = df;
    result.SetGreeks(greeks);
    return result;
}

}  // namespace

numeraire::PricingEngineType AnalyticCommodityFuturesForwardPricer::EngineKind() const {
    return numeraire::PricingEngineType::kAnalytic;
}

core::PricingResult AnalyticCommodityFuturesForwardPricer::Price(const core::IProduct& product,
                                                                const core::IMarketData& market) const {
    if (const auto* fwd = dynamic_cast<const products::CommodityFuturesForwardProduct*>(&product)) {
        return PriceCommodityFuturesForward(*fwd, market);
    }
    throw ValidationError(
            "AnalyticCommodityFuturesForwardPricer requires CommodityFuturesForwardProduct");
}

}  // namespace numeraire::pricers
