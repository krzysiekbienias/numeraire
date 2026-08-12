#pragma once

#include <numeraire/core/iproduct.hpp>
#include <numeraire/database/dtos.hpp>
#include <numeraire/database/itrade_repository.hpp>

#include <memory>

namespace numeraire::products {

/// Builds `core::IProduct` instances from persisted catalog rows. Routing prefers
/// `ProductDto::catalog_instrument_type` (`products_equity.instrument_type` or
/// `products_commodity.instrument_type`) when set; otherwise `instrument_type`
/// inside `structured_params`/attributes JSON as before.
///
/// Equity / index: `MakeFromEquityCatalog`. Commodity futures:
/// `MakeFromCommodityCatalog`. `MakeFromCatalogLeg` picks the branch from the
/// loaded `TradeLegCatalogRow`.
class ProductFactory {
   public:
    [[nodiscard]] static std::unique_ptr<core::IProduct> MakeFromEquityCatalog(
            const database::ProductDto& product, const database::ProductEquityDto& equity,
            const database::TradeHeaderDto* trade_header);

    [[nodiscard]] static std::unique_ptr<core::IProduct> MakeFromCommodityCatalog(
            const database::ProductDto& product, const database::ProductEquityDto& header,
            const database::ProductCommodityDto& commodity,
            const database::TradeHeaderDto* trade_header);

    [[nodiscard]] static std::unique_ptr<core::IProduct> MakeFromCatalogLeg(
            const database::TradeLegCatalogRow& row, const database::TradeHeaderDto* trade_header);
};

}  // namespace numeraire::products
