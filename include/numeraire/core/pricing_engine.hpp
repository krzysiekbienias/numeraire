#pragma once

#include <numeraire/core/imarket_data.hpp>
#include <numeraire/core/ipricer.hpp>
#include <numeraire/core/iproduct.hpp>

namespace numeraire::core {

/// Thin orchestration entry point: validates nothing beyond what `IPricer` does;
/// forwards to `IPricer::Price` so call sites have a single pricing API and room
/// for future overloads / policy without growing `IPricer`.
class PricingEngine {
   public:
    PricingEngine() = delete;
    ~PricingEngine() = default;

    PricingEngine(const PricingEngine&) = delete;
    PricingEngine& operator=(const PricingEngine&) = delete;
    PricingEngine(PricingEngine&&) = delete;
    PricingEngine& operator=(PricingEngine&&) = delete;

    [[nodiscard]] static PricingResult Price(const IProduct& product, const IPricer& pricer,
                                             const IMarketData& market) {
        return pricer.Price(product, market);
    }
};

}  // namespace numeraire::core
