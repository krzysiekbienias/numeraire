#pragma once

#include <cstdint>

namespace numeraire {

/// Business-day adjustment rule (maps to QuantLib::BusinessDayConvention).
enum class DateConvention : std::uint8_t {
    kFollowing,
    kModifiedFollowing,
    kPreceding,
    kModifiedPreceding,
    kUnadjusted,
    kHalfMonthModifiedFollowing,
    kNearest,
};

}  // namespace numeraire
