#include <numeraire/pricers/analytic_futures_outright_pricer.hpp>

#include <numeraire/core/imarket_data.hpp>
#include <numeraire/core/pricing_result.hpp>
#include <numeraire/products/commodity_futures_outright_product.hpp>
#include <numeraire/utils/exception.hpp>

namespace numeraire::pricers {

namespace {

[[nodiscard]] core::PricingResult PriceFuturesOutright(
        const products::CommodityFuturesOutrightProduct& fut, const core::IMarketData& market) {
    const double pv_unit = market.Spot(fut.UnderlyingId());

    core::PricingResult result;
    result.SetNpv(pv_unit);

    core::PricingGreeks greeks;
    greeks.delta = 1.0;
    greeks.gamma = 0.0;
    greeks.vega = 0.0;
    greeks.theta = 0.0;
    greeks.rho = 0.0;
    result.SetGreeks(greeks);
    return result;
}

}  // namespace

numeraire::PricingEngineType AnalyticFuturesOutrightPricer::EngineKind() const {
    return numeraire::PricingEngineType::kAnalytic;
}

core::PricingResult AnalyticFuturesOutrightPricer::Price(const core::IProduct& product,
                                                         const core::IMarketData& market) const {
    if (const auto* fut = dynamic_cast<const products::CommodityFuturesOutrightProduct*>(&product)) {
        return PriceFuturesOutright(*fut, market);
    }
    throw ValidationError("AnalyticFuturesOutrightPricer requires CommodityFuturesOutrightProduct");
}

}  // namespace numeraire::pricers
