#pragma once

#include <numeraire/core/imarket_data.hpp>
#include <numeraire/database/vol_surface_eod_read.hpp>
#include <numeraire/schedule/date.hpp>

#include <memory>
#include <string>
#include <unordered_map>

namespace numeraire::market_data {

/// `IMarketData` backed by persisted EOD vol surfaces (SQLite) with 2D interpolation.
class SqliteVolSurfaceMarketData final : public core::IMarketData {
   public:
    SqliteVolSurfaceMarketData(schedule::Date valuation_date,
                               std::unordered_map<std::string, double> spots,
                               double risk_free_rate,
                               std::unordered_map<std::string, double> dividend_yields,
                               std::unordered_map<std::string, database::VolSurfaceEodRead> surfaces);

    /// Loads surfaces for each underlying in `underlying_ids` from the database.
    [[nodiscard]] static std::unique_ptr<SqliteVolSurfaceMarketData> Load(
            const std::string& database_file_path,
            schedule::Date valuation_date,
            std::unordered_map<std::string, double> spots,
            double risk_free_rate,
            std::unordered_map<std::string, double> dividend_yields,
            const std::vector<std::string>& underlying_ids,
            std::string_view as_of_iso_yyyy_mm_dd,
            std::string_view surface_kind = "implied_bs_eod");

    [[nodiscard]] const schedule::Date& ValuationDate() const override;

    [[nodiscard]] double Spot(std::string_view underlying_id) const override;

    [[nodiscard]] double RiskFreeRate() const override;

    [[nodiscard]] double DividendYield(std::string_view underlying_id) const override;

    [[nodiscard]] double ImpliedVolatility(std::string_view underlying_id,
                                           double strike,
                                           double time_to_expiry_years,
                                           OptionType option_kind) const override;

    [[nodiscard]] const database::VolSurfaceEodRead* FindSurface(std::string_view underlying_id) const noexcept;

   private:
    schedule::Date valuation_date_;
    std::unordered_map<std::string, double> spots_;
    double risk_free_rate_;
    std::unordered_map<std::string, double> dividend_yields_;
    std::unordered_map<std::string, database::VolSurfaceEodRead> surfaces_;
};

}  // namespace numeraire::market_data
