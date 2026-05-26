#pragma once

#include <numeraire/core/ipricer.hpp>

namespace numeraire::pricers {

/// Closed-form **carry / linear forward** pricer (no implied vol).
/// v1: `EquityForwardProduct` only — \(S e^{-qT} - K e^{-rT}\).
/// FX forward and FRA will extend this pricer (or a sibling) later, not
/// `AnalyticBlackScholesEquityPricer`.
class AnalyticForwardPricer final : public core::IPricer {
   public:
    [[nodiscard]] numeraire::PricingEngineType EngineKind() const override;

    [[nodiscard]] core::PricingResult Price(const core::IProduct& product,
                                            const core::IMarketData& market) const override;
};

}  // namespace numeraire::pricers
