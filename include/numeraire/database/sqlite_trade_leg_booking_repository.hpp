#pragma once

#include <numeraire/database/trade_leg_booking_update.hpp>

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace numeraire::database {

/// Persists booking outputs on the structural book: `trade_legs.execution_price` and
/// optional `trades.booking_timestamp`. Does not touch MTM tables or `commission`.
class SqliteTradeLegBookingRepository {
   public:
    explicit SqliteTradeLegBookingRepository(const std::string& database_file_path);
    ~SqliteTradeLegBookingRepository();

    SqliteTradeLegBookingRepository(const SqliteTradeLegBookingRepository&) = delete;
    SqliteTradeLegBookingRepository& operator=(const SqliteTradeLegBookingRepository&) = delete;
    SqliteTradeLegBookingRepository(SqliteTradeLegBookingRepository&&) = delete;
    SqliteTradeLegBookingRepository& operator=(SqliteTradeLegBookingRepository&&) = delete;

    /// `execution_price` must be non-negative. Throws if `leg_id` is missing.
    void UpdateExecutionPrice(std::string_view leg_id, double execution_price) const;

    /// Updates each leg by `leg_id`. Throws if any `leg_id` is missing.
    void UpdateExecutionPrices(std::span<const TradeLegBookingUpdate> updates) const;

    /// When `booking_timestamp` is null or empty, uses SQLite `datetime('now')`.
    /// Throws if `trade_id` is missing.
    void SetTradeBookingTimestamp(std::string_view trade_id,
                                  const std::optional<std::string>& booking_timestamp = std::nullopt) const;

    /// Updates `trades.status` (e.g. `PENDING` → `LIVE` after successful booking).
    void SetTradeStatus(std::string_view trade_id, std::string_view status) const;

    /// All leg updates and optional header timestamp in one transaction.
    /// Leg updates use `WHERE leg_id = ? AND trade_id = ?` so legs must belong to `trade_id`.
    void ApplyTradeBooking(std::string_view trade_id,
                           std::span<const TradeLegBookingUpdate> leg_updates,
                           const std::optional<std::string>& booking_timestamp = std::nullopt) const;

   private:
    void UpdateExecutionPriceForTrade(std::string_view trade_id,
                                      std::string_view leg_id,
                                      double execution_price) const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace numeraire::database
