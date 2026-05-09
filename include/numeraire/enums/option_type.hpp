#pragma once

#include <cstdint>

namespace numeraire {

/// Vanilla option payoff direction (maps to QuantLib::Option::Type).
enum class OptionType : std::uint8_t {
    kCall,
    kPut,
};

}  // namespace numeraire
