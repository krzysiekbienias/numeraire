#pragma once

#include <numeraire/core/ipricer.hpp>

namespace numeraire::pricers {

/// European equity vanilla via **explicit Black–Scholes closed form** (d₁, d₂,
/// Φ(·), NPV and textbook greeks). QuantLib is **not** used here—only for
/// benchmarks in unit tests and for calendar math via `schedule::Act365FixedYearFraction`.
/// Expects `products::VanillaEquityOptionProduct`; other `IProduct` types throw
/// `ValidationError`. `IMarketData`: continuous `r` and `q`, flat vol from
/// `ImpliedVolatility(underlying, K, T)` with `T` = Act/365 from trade to expiry.
class AnalyticBlackScholesEquityPricer final : public core::IPricer {
   public:
    [[nodiscard]] numeraire::PricingEngineType EngineKind() const override;

    [[nodiscard]] core::PricingResult Price(const core::IProduct& product,
                                            const core::IMarketData& market) const override;
};

}  // namespace numeraire::pricers
