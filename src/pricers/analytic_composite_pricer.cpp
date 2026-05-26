#include <numeraire/core/iproduct.hpp>
#include <numeraire/pricers/analytic_composite_pricer.hpp>
#include <numeraire/products/equity_forward_product.hpp>

namespace numeraire::pricers {

numeraire::PricingEngineType AnalyticCompositePricer::EngineKind() const {
    return numeraire::PricingEngineType::kAnalytic;
}

core::PricingResult AnalyticCompositePricer::Price(const core::IProduct& product,
                                                   const core::IMarketData& market) const {
    if (dynamic_cast<const products::EquityForwardProduct*>(&product) != nullptr) {
        return forwards_.Price(product, market);
    }
    return equity_options_.Price(product, market);
}

}  // namespace numeraire::pricers
