#pragma once

#include <cstdint>

namespace numeraire {

/// Schedule date-generation rule (maps to QuantLib::DateGeneration::Rule).
enum class DateGenerationRule : std::uint8_t {
    kBackward,
    kForward,
    kZero,
    kThirdWednesday,
    kThirdWednesdayInclusive,
    kTwentieth,
    kTwentiethImm,
    kOldCds,
    kCds,
    kCds2015,
};

}  // namespace numeraire
