#pragma once

#include <numeraire/core/iproduct.hpp>
#include <numeraire/database/dtos.hpp>

#include <memory>

namespace numeraire::products {

/// Builds `core::IProduct` instances from persisted catalog rows. Parses
/// `instrument_type` inside `attributes_json`: omitted / vanilla maps to
/// `VanillaEquityOptionProduct`; `AssetOrNothingOption` maps to
/// `EquityAssetOrNothingProduct`. Other values throw `ValidationError` until
/// wired.
class ProductFactory {
   public:
    [[nodiscard]] static std::unique_ptr<core::IProduct> MakeFromEquityCatalog(
            const database::ProductDto& product, const database::ProductEquityDto& equity,
            const database::TradeDto* trade);
};

}  // namespace numeraire::products
