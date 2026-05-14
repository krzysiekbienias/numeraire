#pragma once

#include <numeraire/database/dtos.hpp>

#include <string_view>

namespace numeraire::database {

/// Single trade with the catalog rows needed to build `core::IProduct` via
/// `ProductFactory::MakeFromEquityCatalog`. Loaded atomically by `ITradeRepository`.
struct TradeCatalogBundle {
    TradeDto trade;
    ProductDto product;
    ProductEquityDto equity;
};

/// Persistence port for trade + product catalog reads. Implementations (SQLite,
/// in-memory, …) live under `src/database/`.
///
/// `GetCatalogForTrade`:
/// — **0 rows** → `PersistenceError` (trade unknown or incomplete join).
/// — **1 row** → filled bundle.
/// — **>1 row** → `PersistenceError` (data integrity).
class ITradeRepository {
   protected:
    ITradeRepository() = default;

   public:
    virtual ~ITradeRepository() = default;

    ITradeRepository(const ITradeRepository&) = delete;
    ITradeRepository& operator=(const ITradeRepository&) = delete;
    ITradeRepository(ITradeRepository&&) = delete;
    ITradeRepository& operator=(ITradeRepository&&) = delete;

    [[nodiscard]] virtual TradeCatalogBundle GetCatalogForTrade(std::string_view trade_id) const = 0;
};

}  // namespace numeraire::database
