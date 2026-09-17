#pragma once

#include <numeraire/core/ipricer.hpp>

namespace numeraire::pricers {

/// Uncollateralized forward on a listed commodity futures quote \f$F_{t,T}\f$.
/// `pv_unit` \f$= e^{-r\tau}(F - K)\f$; unit delta \f$= e^{-r\tau}\f$. \f$F\f$ is
/// `Spot(contract_ticker)` (the same settle map as listed outrights). No vol.
class AnalyticCommodityFuturesForwardPricer final : public core::IPricer {
   public:
    [[nodiscard]] numeraire::PricingEngineType EngineKind() const override;

    [[nodiscard]] core::PricingResult Price(const core::IProduct& product,
                                            const core::IMarketData& market) const override;
};

}  // namespace numeraire::pricers
