#pragma once

#include <cstdint>

namespace numeraire {

/// Settlement / schedule calendar identifier (maps to QuantLib calendar factories).
enum class CalendarType : std::uint8_t {
    kUnitedKingdom,
    kUnitedStates,
    kTarget,
    kGermany,
};

}  // namespace numeraire
