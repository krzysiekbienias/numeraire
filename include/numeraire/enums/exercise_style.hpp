#pragma once

#include <cstdint>

namespace numeraire {

/// When the holder may exercise (maps to QuantLib::Exercise::Type).
enum class ExerciseStyle : std::uint8_t {
    kEuropean,
    kAmerican,
    kBermudan,
};

}  // namespace numeraire
