#pragma once

#include <numeraire/core/pricing_result.hpp>
#include <numeraire/enums/pricing_engine_type.hpp>

namespace numeraire::core {

class IProduct;
class IMarketData;

/// Contract for an instrument pricer (analytic, MC, tree, …). Implementations
/// live outside `core` and typically hold or resolve `IModel` state so that
/// `PricingEngine::Price(product, pricer, market)` stays a thin dispatch.
class IPricer {
   protected:
    IPricer() = default;

   public:
    virtual ~IPricer() = default;

    IPricer(const IPricer&) = delete;
    IPricer& operator=(const IPricer&) = delete;
    IPricer(IPricer&&) = delete;
    IPricer& operator=(IPricer&&) = delete;

    [[nodiscard]] virtual numeraire::PricingEngineType EngineKind() const = 0;

    [[nodiscard]] virtual PricingResult Price(const IProduct& product,
                                              const IMarketData& market) const = 0;
};

}  // namespace numeraire::core
