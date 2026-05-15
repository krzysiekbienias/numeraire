#pragma once

#include <numeraire/database/itrade_repository.hpp>

#include <memory>
#include <string>

namespace numeraire::database {

/// SQLite-backed `ITradeRepository` via [SQLiteCpp](https://github.com/SRombauts/SQLiteCpp)
/// (links the system `sqlite3`). Expects schema compatible with
/// [`sql/schema_v1.sql`](../../../../sql/schema_v1.sql): `trades`, `trade_legs`,
/// `products`, `products_equity` with one result row per leg for a given `trade_id`.
class SqliteTradeRepository final : public ITradeRepository {
   public:
    explicit SqliteTradeRepository(std::string database_file_path);
    ~SqliteTradeRepository() override;

    SqliteTradeRepository(const SqliteTradeRepository&) = delete;
    SqliteTradeRepository& operator=(const SqliteTradeRepository&) = delete;
    SqliteTradeRepository(SqliteTradeRepository&&) = delete;
    SqliteTradeRepository& operator=(SqliteTradeRepository&&) = delete;

    [[nodiscard]] TradeCatalogBundle GetCatalogForTrade(std::string_view trade_id) const override;

    [[nodiscard]] std::vector<std::string> ListAllTradeIds() const override;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace numeraire::database
