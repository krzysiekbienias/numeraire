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

/// MTM requires at least one leg, each with a finite `execution_price`
/// (zero and negative booked marks are valid). Status (`LIVE`) is the gate.
void RequireAllLegsBookedForMtm(const TradeCatalogBundle& bundle);

/// MTM `as_of` must not precede the trade's booking date (ISO string compare).
void RequireMtmAsOfNotBeforeTradeDate(std::string_view as_of_iso, const TradeHeaderDto& trade);

}  // namespace numeraire::database
