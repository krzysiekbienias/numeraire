#pragma once

#include <numeraire/core/ipricer.hpp>

namespace numeraire::pricers {

/// Mark-to-market pricer for listed `CommodityFuturesOutrightProduct`.
/// `pv_unit = Spot(contract_ticker)` (settle loaded into the spot map);
/// unit delta = 1. No vol, rates, or day-count — daily exchange margining.
class AnalyticFuturesOutrightPricer final : public core::IPricer {
   public:
    [[nodiscard]] numeraire::PricingEngineType EngineKind() const override;

    [[nodiscard]] core::PricingResult Price(const core::IProduct& product,
                                            const core::IMarketData& market) const override;
};

}  // namespace numeraire::pricers
