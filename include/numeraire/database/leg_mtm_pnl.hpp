#pragma once

#include <numeraire/database/itrade_repository.hpp>
#include <numeraire/database/prior_official_mtm_mark.hpp>

#include <optional>

namespace numeraire::database {

/// Booked entry mark: `LegPvTotal(direction, quantity, contract_size, execution_price)`.
/// Uses `trade_legs.execution_price` (per-share premium at booking; commission excluded).
[[nodiscard]] double LegBookedMark(const TradeLegCatalogRow& row) noexcept;

/// `trade_legs.commission` when set, else `0`.
[[nodiscard]] double LegCommissionOrZero(const TradeLegDto& leg) noexcept;

/// Prior `pv_total` for `pnl_daily`: official MTM row when present, else [`LegBookedMark`].
[[nodiscard]] double ResolvePvTotalPrevForDaily(const std::optional<PriorOfficialMtmMark>& prior_official_mark,
                                                double booked_mark) noexcept;

/// `pv_total − pv_total_prev` (commission excluded).
[[nodiscard]] double LegPnlDaily(double pv_total, double pv_total_prev) noexcept;

/// `pv_total − booked_mark − commission`.
[[nodiscard]] double LegPnlInception(double pv_total, double booked_mark, double commission) noexcept;

}  // namespace numeraire::database
