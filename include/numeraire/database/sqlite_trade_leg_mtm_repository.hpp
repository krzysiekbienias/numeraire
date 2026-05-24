#pragma once

#include <numeraire/database/prior_official_mtm_mark.hpp>
#include <numeraire/database/trade_leg_mtm_eod_row.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace numeraire::database {

/// Persists EOD MTM rows to `trade_leg_mtm_eod` (`INSERT OR REPLACE`) and, when
/// `batch_run_id` is set, append-only `trade_leg_mtm_eod_archive` for run comparison.
///
/// **PnL prior lookup** reads the **official** table `trade_leg_mtm_eod` only (current mark
/// per `leg_id`, `as_of`, `pricing_engine`). The archive is for batch audit / run diff, not
/// for `pnl_daily` prior marks.
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

    /// Largest `as_of` strictly before `as_of` for `(leg_id, pricing_engine)` in
    /// `trade_leg_mtm_eod`. Empty when no prior official mark exists (first EOD after booking).
    [[nodiscard]] std::optional<PriorOfficialMtmMark> LookupPriorOfficialMark(std::string_view leg_id,
                                                                              std::string_view pricing_engine,
                                                                              std::string_view as_of) const;

   private:
    void InsertArchive(const TradeLegMtmEodRow& row, const std::string& calculated_at) const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace numeraire::database
