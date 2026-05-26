#pragma once

#include <numeraire/core/ipricer.hpp>

namespace numeraire::pricers {

/// European equity **options** via explicit Black–Scholes closed form: vanilla,
/// asset-or-nothing, cash-or-nothing. For forwards use `AnalyticForwardPricer` or
/// `AnalyticCompositePricer` from `PricerFactory`.
/// QuantLib is **not** used here—only for benchmarks in unit tests.
/// Expects `VanillaEquityOptionProduct`, `EquityAssetOrNothingProduct`, or
/// `EquityCashOrNothingProduct`. `IMarketData`: continuous `r` and `q`, flat vol from
/// `ImpliedVolatility(underlying, K, T)` with `T` = Act/365 from valuation to expiry.
class AnalyticBlackScholesEquityPricer final : public core::IPricer {
   public:
    [[nodiscard]] numeraire::PricingEngineType EngineKind() const override;

    [[nodiscard]] core::PricingResult Price(const core::IProduct& product,
                                            const core::IMarketData& market) const override;
};

}  // namespace numeraire::pricers
