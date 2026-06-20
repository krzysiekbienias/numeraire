#pragma once

#include <numeraire/database/itrade_repository.hpp>
#include <numeraire/schedule/date.hpp>

#include <string>
#include <string_view>

namespace numeraire::database {

inline constexpr std::string_view kTradeStatusPending = "PENDING";
inline constexpr std::string_view kTradeStatusLive = "LIVE";
inline constexpr std::string_view kTradeStatusExpired = "EXPIRED";

[[nodiscard]] bool TradeStatusEquals(std::string_view actual, std::string_view expected) noexcept;

/// Non-empty ISO `YYYY-MM-DD` on the trade header.
[[nodiscard]] std::string RequireTradeDateIso(const TradeHeaderDto& trade);

/// Booking invariant: pricing as-of must be the trade's booking calendar date.
void RequireValuationDateEqualsTradeDate(const schedule::Date& valuation_date,
                                         const TradeHeaderDto& trade);

/// Booking run allowed only while the trade is still `PENDING`.
void RequireTradePendingForBooking(const TradeHeaderDto& trade);

/// MTM run allowed only after the trade is `LIVE`.
void RequireTradeLiveForMtm(const TradeHeaderDto& trade);

/// Every leg must have a booked premium before MTM (`execution_price` strictly positive).
void RequireAllLegsBookedForMtm(const TradeCatalogBundle& bundle);

/// MTM `as_of` must not precede the trade's booking date (ISO string compare).
void RequireMtmAsOfNotBeforeTradeDate(std::string_view as_of_iso, const TradeHeaderDto& trade);

/// After booking pricer: every leg must have `execution_price > 0` to promote to `LIVE`.
[[nodiscard]] bool AllLegExecutionPricesPositive(const TradeCatalogBundle& bundle);

}  // namespace numeraire::database
