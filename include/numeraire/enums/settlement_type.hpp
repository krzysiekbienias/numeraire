#pragma once

#include <cstdint>

namespace numeraire {

/// Delivery mechanism upon product expiry.
enum class SettlementType : std::uint8_t {
    kCash,     // Rozliczenie różnicy w gotówce (np. WIG20)
    kPhysical  // Fizyczna dostawa instrumentu (np. Akcje, Złoto)
};

}  // namespace numeraire
