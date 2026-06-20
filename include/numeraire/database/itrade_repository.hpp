#pragma once

#include <numeraire/database/dtos.hpp>

#include <string_view>
#include <vector>

namespace numeraire::database {

/// One leg with the catalog rows needed to build `core::IProduct` for that leg.
struct TradeLegCatalogRow {
    TradeLegDto leg;
    ProductDto product;
    ProductEquityDto equity;
};

/// Trade header plus all legs (each with product + equity facet for pricing).
struct TradeCatalogBundle {
    TradeHeaderDto trade;
    std::vector<TradeLegCatalogRow> legs;
};

/// Persistence port for trade + product catalog reads. Implementations (SQLite,
/// in-memory, …) live under `src/database/`.
///
/// `GetCatalogForTrade`:
/// — **0 legs** / no trade → `PersistenceError` (trade unknown or incomplete join).
/// — **≥1 leg** → bundle with shared header and one row per leg.
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

    /// All `trade_id` values in `trades`, sorted ascending (for batch / dev tooling).
    [[nodiscard]] virtual std::vector<std::string> ListAllTradeIds() const = 0;

    /// LIVE trades for one `trades.portfolio_id`, sorted ascending by `trade_id`.
    [[nodiscard]] virtual std::vector<std::string> ListLiveTradeIdsForPortfolio(
            std::string_view portfolio_id) const = 0;
};

}  // namespace numeraire::database
