#pragma once

#include <numeraire/database/underlying_daily_closes.hpp>
#include <numeraire/quant/cholesky.hpp>
#include <numeraire/schedule/date.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace numeraire::simulation {

struct HistoricalCalibratorConfig {
    schedule::Date as_of{};
    /// Calendar lookback from `as_of` when loading EOD history from SQLite.
    int lookback_calendar_days = 504;
    /// Minimum aligned log-return observations required across all factors.
    std::size_t min_return_observations = 60;
    int adjusted = 1;
    /// Annualization factor for historical vol (`sigma_daily * sqrt(N)`).
    int vol_annualization_days = 252;
};

struct HistoricalCalibrationResult {
    std::vector<std::string> factor_ids;
    /// Spot closes on `as_of` per factor (same order as `factor_ids`).
    std::vector<double> spots_as_of;
    /// Annualized historical volatilities per factor.
    std::vector<double> volatilities;
    /// Nearest correlation matrix, row-major (`n * n`), unit diagonal, PSD.
    std::vector<double> correlation;
    quant::CholeskyFactor cholesky;
    std::size_t num_return_observations{0};
    schedule::Date history_start{};
    schedule::Date history_end{};
};

/// Calibrate vol / correlation / Cholesky from aligned daily close history (no DB I/O).
[[nodiscard]] HistoricalCalibrationResult CalibrateFromPriceHistory(
        const std::vector<std::string>& factor_ids,
        const std::unordered_map<std::string, std::vector<database::DailyCloseObservation>>& closes_by_factor,
        const HistoricalCalibratorConfig& config);

/// Calibrate explicit factor list using EOD history loaded from SQLite on `[as_of - lookback, as_of]`.
[[nodiscard]] HistoricalCalibrationResult CalibrateFromDatabase(
        const std::string& database_file_path,
        const std::vector<std::string>& factor_ids,
        const HistoricalCalibratorConfig& config);

/// Calibrate distinct book underlyings (LIVE trades only).
/// `portfolio_id` unset → all portfolios (`scope_key = ALL` when persisting).
/// `portfolio_id` set → only legs from that `trades.portfolio_id`.
[[nodiscard]] HistoricalCalibrationResult CalibrateBookFromDatabase(
        const std::string& database_file_path,
        const HistoricalCalibratorConfig& config,
        std::optional<std::string_view> portfolio_id = std::nullopt);

}  // namespace numeraire::simulation
