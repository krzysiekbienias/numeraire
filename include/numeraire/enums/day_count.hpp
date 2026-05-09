#pragma once

#include <cstdint>

namespace numeraire {

/// Day-count convention for accrual (maps to QuantLib::DayCounter instances).
enum class DayCount : std::uint8_t {
    kActual360,
    kActual365Fixed,
    kThirty360,
    kActualActualIsda,
    kActualActualBond,
    kBusiness252,
};

}  // namespace numeraire
