#include <numeraire/pricers/analytic_spot_pricer.hpp>

#include <numeraire/core/imarket_data.hpp>
#include <numeraire/core/pricing_result.hpp>
#include <numeraire/products/equity_spot_product.hpp>
#include <numeraire/utils/exception.hpp>

namespace numeraire::pricers {

namespace {

[[nodiscard]] core::PricingResult PriceSpot(const products::EquitySpotProduct& spot,
                                            const core::IMarketData& market) {
    const double pv_unit = market.Spot(spot.UnderlyingId());

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

numeraire::PricingEngineType AnalyticSpotPricer::EngineKind() const {
    return numeraire::PricingEngineType::kAnalytic;
}

core::PricingResult AnalyticSpotPricer::Price(const core::IProduct& product,
                                              const core::IMarketData& market) const {
    if (const auto* spot = dynamic_cast<const products::EquitySpotProduct*>(&product)) {
        return PriceSpot(*spot, market);
    }
    throw ValidationError("AnalyticSpotPricer requires EquitySpotProduct");
}

}  // namespace numeraire::pricers
