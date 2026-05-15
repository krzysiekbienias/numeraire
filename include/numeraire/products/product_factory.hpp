#pragma once

#include <numeraire/core/iproduct.hpp>
#include <numeraire/database/dtos.hpp>

#include <memory>

namespace numeraire::products {

/// Builds `core::IProduct` instances from persisted catalog rows. Routing prefers
/// `ProductDto::catalog_instrument_type` (`products_equity.instrument_type`) when
/// set; otherwise `instrument_type` inside `structured_params`/attributes JSON as
/// before. Omit / vanilla ⇒ `VanillaEquityOptionProduct`; AoN synonyms ⇒
/// `EquityAssetOrNothingProduct`. `catalog_exercise_style` defaults to european.
class ProductFactory {
   public:
    [[nodiscard]] static std::unique_ptr<core::IProduct> MakeFromEquityCatalog(
            const database::ProductDto& product, const database::ProductEquityDto& equity,
            const database::TradeHeaderDto* trade_header);
};

}  // namespace numeraire::products
