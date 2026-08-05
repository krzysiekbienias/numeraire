#pragma once

#include <numeraire/core/ipricer.hpp>

#include <cstddef>
#include <cstdint>

namespace numeraire::pricers {

/// European equity vanilla by Monte Carlo under GBM (`PricingEngineType::kMonteCarlo`).
/// Same `IMarketData` quotes as the analytic BS pricer.
///
/// The payoff only looks at the spot at maturity, so the terminal value is drawn in a
/// **single exact GBM step** — independent Wiener increments make intermediate points
/// irrelevant here. There is therefore no discretisation bias: the only error is
/// sampling noise, reported as the standard error in `PricingMetadata::diagnostics`.
/// Path-dependent payoffs (Asian, barrier) will need a real time grid instead.
///
/// Expects a European `VanillaEquityOptionProduct`; American exercise belongs to the
/// tree. Greeks are **not** computed — this engine is informational next to the
/// official analytic mark.
///
/// Defaults: `NUMERAIRE_MC_PRICER_PATHS` / `NUMERAIRE_MC_PRICER_SEED` env, else
/// `configs/default.json` → `pricing.monte_carlo_pricer.*`, else the fallbacks below.
/// These are separate from the `NUMERAIRE_MC_*` / `pricing.monte_carlo` settings that
/// size the exposure simulation.
class MonteCarloGbmEuropeanPricer final : public core::IPricer {
   public:
    /// Last-resort fallbacks if env and `configs/default.json` are unavailable.
    static constexpr std::size_t kFallbackPaths = 10000;
    static constexpr std::uint64_t kFallbackSeed = 42;

    /// Resolve path count and seed from env / committed config (see class note).
    MonteCarloGbmEuropeanPricer();

    /// Explicit settings (unit tests / callers that already resolved them).
    MonteCarloGbmEuropeanPricer(std::size_t num_paths, std::uint64_t seed);

    [[nodiscard]] numeraire::PricingEngineType EngineKind() const override;

    [[nodiscard]] core::PricingResult Price(const core::IProduct& product,
                                            const core::IMarketData& market) const override;

    /// Recorded per mark so a stored price can be reproduced exactly.
    [[nodiscard]] std::size_t NumPaths() const { return num_paths_; }
    [[nodiscard]] std::uint64_t Seed() const { return seed_; }

   private:
    std::size_t num_paths_;
    std::uint64_t seed_;
};

}  // namespace numeraire::pricers
