#pragma once

#include <numeraire/schedule/date.hpp>

namespace numeraire::simulation {

/// Path-wise repricing runs only on grid nodes on or before the leg expiry (European EOD).
[[nodiscard]] bool IsLegActiveOnGridNode(const schedule::Date& node_date,
                                         const schedule::Date& expiry_date) noexcept;

}  // namespace numeraire::simulation
