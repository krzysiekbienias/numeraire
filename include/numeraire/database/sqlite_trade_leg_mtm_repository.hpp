#pragma once

#include <numeraire/database/trade_leg_mtm_eod_row.hpp>

#include <memory>
#include <string>

namespace numeraire::database {

/// Persists EOD MTM rows to `trade_leg_mtm_eod` (`INSERT OR REPLACE`) and, when
/// `batch_run_id` is set, append-only `trade_leg_mtm_eod_archive` for run comparison.
class SqliteTradeLegMtmRepository {
   public:
    explicit SqliteTradeLegMtmRepository(const std::string& database_file_path);
    ~SqliteTradeLegMtmRepository();

    SqliteTradeLegMtmRepository(const SqliteTradeLegMtmRepository&) = delete;
    SqliteTradeLegMtmRepository& operator=(const SqliteTradeLegMtmRepository&) = delete;
    SqliteTradeLegMtmRepository(SqliteTradeLegMtmRepository&&) = delete;
    SqliteTradeLegMtmRepository& operator=(SqliteTradeLegMtmRepository&&) = delete;

    /// When `batch_run_id` is set: append archive row, then upsert official mark.
    void Upsert(const TradeLegMtmEodRow& row) const;

   private:
    void InsertArchive(const TradeLegMtmEodRow& row, const std::string& calculated_at) const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace numeraire::database
