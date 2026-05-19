#pragma once

#include <numeraire/enums/position_direction.hpp>

#include <string_view>

namespace numeraire::database {

[[nodiscard]] double PositionSign(numeraire::PositionDirection direction) noexcept;

/// Position mark-to-model PV: `sign(direction) × quantity × contract_size × pv_unit`.
///
/// `pv_unit` is model NPV per one unit of underlying (one share); `contract_size` is
/// shares per listed contract (e.g. 100 for US equity options).
[[nodiscard]] double LegPvTotal(numeraire::PositionDirection direction,
                                double quantity,
                                double contract_size,
                                double pv_unit) noexcept;

void RequirePositiveContractSize(double contract_size, std::string_view leg_id);

/// Booked leg size in contracts; must be strictly positive (direction is on `trade_legs.direction`).
void RequirePositiveQuantity(double quantity, std::string_view leg_id);

}  // namespace numeraire::database
