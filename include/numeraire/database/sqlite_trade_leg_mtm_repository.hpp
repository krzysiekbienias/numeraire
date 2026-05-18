#pragma once

#include <numeraire/database/trade_leg_mtm_eod_row.hpp>

#include <memory>
#include <string>

namespace numeraire::database {

/// Writes to SQLite table `trade_leg_mtm_eod` using `INSERT OR REPLACE` keyed by
/// `UNIQUE (leg_id, as_of, pricing_engine)` in [schema_v1.sql](../../../../sql/schema_v1.sql).
class SqliteTradeLegMtmRepository {
   public:
    explicit SqliteTradeLegMtmRepository(const std::string& database_file_path);
    ~SqliteTradeLegMtmRepository();

    SqliteTradeLegMtmRepository(const SqliteTradeLegMtmRepository&) = delete;
    SqliteTradeLegMtmRepository& operator=(const SqliteTradeLegMtmRepository&) = delete;
    SqliteTradeLegMtmRepository(SqliteTradeLegMtmRepository&&) = delete;
    SqliteTradeLegMtmRepository& operator=(SqliteTradeLegMtmRepository&&) = delete;

    void Upsert(const TradeLegMtmEodRow& row) const;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace numeraire::database
