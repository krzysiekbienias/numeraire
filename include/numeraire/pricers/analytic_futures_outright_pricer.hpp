#pragma once

#include <numeraire/core/ipricer.hpp>

namespace numeraire::pricers {

/// Mark-to-market pricer for listed `CommodityFuturesOutrightProduct`.
/// `pv_unit` is \f$F - K\f$, where \f$F\f$ is `Quote(contract_ticker)` (settle
/// loaded into the spot map) and \f$K\f$ is `DeliveryPrice()` (the trade's entry price).
/// Undiscounted, unit delta \f$= 1\f$. No vol, rates, or day-count — daily exchange margining.
class AnalyticFuturesOutrightPricer final : public core::IPricer {
public:
    [[nodiscard]] numeraire::PricingEngineType EngineKind() const override;

    [[nodiscard]] core::PricingResult Price(const core::IProduct& product,
                                            const core::IMarketData& market) const override;
};

}  // namespace numeraire::pricers
