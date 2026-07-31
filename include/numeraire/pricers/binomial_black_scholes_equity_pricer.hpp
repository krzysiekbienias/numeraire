#pragma once

#include <numeraire/core/ipricer.hpp>

#include <cstddef>

namespace numeraire::pricers {

/// Equity vanilla via Cox–Ross–Rubinstein tree (`PricingEngineType::kBinomialTree`).
/// Same `IMarketData` quotes as the analytic BS pricer; American early exercise supported.
/// Greeks are **bump-and-reprice** (no closed form on the tree).
///
/// Expects `VanillaEquityOptionProduct` (European or American). Flat tree state in
/// `quant::CoxRossRubinsteinVanillaPrice`.
///
/// Default step count: `NUMERAIRE_BINOMIAL_STEPS` env, else
/// `configs/default.json` → `pricing.binomial.default_steps`, else `kFallbackSteps`.
class BinomialBlackScholesEquityPricer final : public core::IPricer {
   public:
    /// Last-resort fallback if env and `configs/default.json` are unavailable.
    static constexpr std::size_t kFallbackSteps = 200;

    /// Resolve step count from env / committed config (see class note).
    BinomialBlackScholesEquityPricer();

    /// Explicit step count (unit tests / callers that already resolved settings).
    explicit BinomialBlackScholesEquityPricer(std::size_t n_steps);

    [[nodiscard]] numeraire::PricingEngineType EngineKind() const override;

    [[nodiscard]] core::PricingResult Price(const core::IProduct& product,
                                            const core::IMarketData& market) const override;

    [[nodiscard]] std::size_t NSteps() const { return n_steps_; }

   private:
    std::size_t n_steps_;
};

}  // namespace numeraire::pricers
