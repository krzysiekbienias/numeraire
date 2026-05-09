#pragma once

#include <cstdint>

namespace numeraire {

/// ISO-style currency tag (maps to QuantLib::*Currency() factories).
enum class Currency : std::uint8_t {
    kUsd,
    kEur,
    kGbp,
    kJpy,
    kChf,
    kAud,
    kCad,
    kHkd,
    kSgd,
};

}  // namespace numeraire
