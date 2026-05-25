#pragma once

#include <numeraire/core/ipricer.hpp>

namespace numeraire::pricers {

/// European equity **vanilla**, **asset-or-nothing**, and **cash-or-nothing** via explicit
/// Black–Scholes closed form.
/// QuantLib is **not** used here—only for benchmarks in unit tests.
/// Expects `VanillaEquityOptionProduct`, `EquityAssetOrNothingProduct`, or
/// `EquityCashOrNothingProduct`; other `IProduct` types throw `ValidationError`. `IMarketData`: continuous `r` and `q`, flat vol from
/// `ImpliedVolatility(underlying, K, T)` with `T` = Act/365 from valuation to expiry.
class AnalyticBlackScholesEquityPricer final : public core::IPricer {
   public:
    [[nodiscard]] numeraire::PricingEngineType EngineKind() const override;

    [[nodiscard]] core::PricingResult Price(const core::IProduct& product,
                                            const core::IMarketData& market) const override;
};

}  // namespace numeraire::pricers
