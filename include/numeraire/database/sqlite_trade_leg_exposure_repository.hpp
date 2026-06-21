#pragma once

#include <numeraire/database/trade_leg_exposure_eod_row.hpp>

#include <memory>
#include <string>

namespace numeraire::database {

/// Persists MC exposure metrics to `trade_leg_exposure_eod` and append-only archive.
class SqliteTradeLegExposureRepository {
   public:
    explicit SqliteTradeLegExposureRepository(const std::string& database_file_path);
    ~SqliteTradeLegExposureRepository();

    SqliteTradeLegExposureRepository(const SqliteTradeLegExposureRepository&) = delete;
    SqliteTradeLegExposureRepository& operator=(const SqliteTradeLegExposureRepository&) = delete;
    SqliteTradeLegExposureRepository(SqliteTradeLegExposureRepository&&) = delete;
    SqliteTradeLegExposureRepository& operator=(SqliteTradeLegExposureRepository&&) = delete;

    void Upsert(const TradeLegExposureEodRow& row) const;

   private:
    void InsertArchive(const TradeLegExposureEodRow& row, const std::string& calculated_at) const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace numeraire::database
