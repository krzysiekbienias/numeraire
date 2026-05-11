#pragma once

#include <cstdint>

namespace numeraire {

/// Position direction / trade side.
enum class PositionDirection : std::uint8_t {
    kLong,  // Kupiliśmy instrument (np. +100 sztuk)
    kShort  // Sprzedaliśmy/wystawiliśmy instrument (np. -100 sztuk)
};

}  // namespace numeraire
