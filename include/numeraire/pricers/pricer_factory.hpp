#pragma once

#include <numeraire/core/ipricer.hpp>
#include <numeraire/enums/model_type.hpp>
#include <numeraire/enums/pricing_engine_type.hpp>

#include <memory>

namespace numeraire::pricers {

/// Builds `core::IPricer` instances from (`PricingEngineType`, `ModelType`).
/// Unsupported pairs throw `ValidationError` — one central place for routing.
class PricerFactory {
   public:
    [[nodiscard]] static std::unique_ptr<core::IPricer> Make(PricingEngineType engine, ModelType model);
};

}  // namespace numeraire::pricers
