#pragma once

#include <cstdint>

namespace numeraire {

/// Coupon or schedule frequency (subset of QuantLib::Frequency).
enum class Frequency : std::uint8_t {
    kNoFrequency,
    kOnce,
    kAnnual,
    kSemiannual,
    kEveryFourthMonth,
    kQuarterly,
    kBimonthly,
    kMonthly,
    kEveryFourthWeek,
    kBiweekly,
    kWeekly,
    kDaily,
    kOtherFrequency,
};

}  // namespace numeraire
