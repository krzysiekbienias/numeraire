#pragma once
#include <cstdint>

namespace numeraire {
enum class PricingEngineType : std::uint8_t { kAnalytic, kMonteCarlo, kBinomialTree, kFiniteDifference };
}  // namespace numeraire
