#pragma once

#include <numeraire/core/ipricer.hpp>

namespace numeraire::pricers {

/// Mark-to-market spot pricer for `EquitySpotProduct` (cash equity or index).
/// `pv_unit` is `Spot(underlying)`; unit delta \f$= 1\f$. No vol, rates, or day-count.
class AnalyticSpotPricer final : public core::IPricer {
   public:
    [[nodiscard]] numeraire::PricingEngineType EngineKind() const override;

    [[nodiscard]] core::PricingResult Price(const core::IProduct& product,
                                            const core::IMarketData& market) const override;
};

}  // namespace numeraire::pricers
