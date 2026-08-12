#include <numeraire/core/iproduct.hpp>
#include <numeraire/pricers/analytic_composite_pricer.hpp>
#include <numeraire/products/commodity_futures_outright_product.hpp>
#include <numeraire/products/equity_forward_product.hpp>
#include <numeraire/products/equity_spot_product.hpp>

namespace numeraire::pricers {

numeraire::PricingEngineType AnalyticCompositePricer::EngineKind() const {
    return numeraire::PricingEngineType::kAnalytic;
}

core::PricingResult AnalyticCompositePricer::Price(const core::IProduct& product,
                                                   const core::IMarketData& market) const {
    if (dynamic_cast<const products::CommodityFuturesOutrightProduct*>(&product) != nullptr) {
        return futures_outrights_.Price(product, market);
    }
    if (dynamic_cast<const products::EquitySpotProduct*>(&product) != nullptr) {
        return spots_.Price(product, market);
    }
    if (dynamic_cast<const products::EquityForwardProduct*>(&product) != nullptr) {
        return forwards_.Price(product, market);
    }
    return equity_options_.Price(product, market);
}

}  // namespace numeraire::pricers
