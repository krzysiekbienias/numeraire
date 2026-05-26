#pragma once

#include <numeraire/core/ipricer.hpp>
#include <numeraire/pricers/analytic_black_scholes_equity_pricer.hpp>
#include <numeraire/pricers/analytic_forward_pricer.hpp>

namespace numeraire::pricers {

/// Routes `IProduct` instances to specialized analytic pricers. Default analytic
/// engine returned by `PricerFactory` for `dev_main` booking / MTM.
/// — equity options (vanilla, binaries) → `AnalyticBlackScholesEquityPricer`
/// — equity forwards → `AnalyticForwardPricer`
class AnalyticCompositePricer final : public core::IPricer {
   public:
    [[nodiscard]] numeraire::PricingEngineType EngineKind() const override;

    [[nodiscard]] core::PricingResult Price(const core::IProduct& product,
                                            const core::IMarketData& market) const override;

   private:
    AnalyticBlackScholesEquityPricer equity_options_{};
    AnalyticForwardPricer forwards_{};
};

}  // namespace numeraire::pricers
