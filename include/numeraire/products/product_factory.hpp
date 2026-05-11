#pragma once

#include <numeraire/core/iproduct.hpp>
#include <numeraire/database/dtos.hpp>

#include <memory>

namespace numeraire::products {

/// Builds `core::IProduct` instances from persisted catalog rows. Parsing of
/// `attributes_json` is intentionally narrow for now: empty object or omitted
/// `instrument_type` maps to a single-leg vanilla; exotic keys throw
/// `ValidationError` until dedicated product types exist.
class ProductFactory {
   public:
    [[nodiscard]] static std::unique_ptr<core::IProduct> MakeFromEquityCatalog(
            const database::ProductDto& product, const database::ProductEquityDto& equity,
            const database::TradeDto* trade);
};

}  // namespace numeraire::products
