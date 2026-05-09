#pragma once

#include <cstdint>

namespace numeraire {

/// Pricing model family (framework taxonomy; no single QuantLib enum).
///
/// Used by factories and configuration; QuantLib mapping happens per concrete
/// engine / model in later sprints.
enum class ModelType : std::uint8_t {
    kBlackScholes,
    kHeston,
    kLocalVolatility,
    kMonteCarlo,
    kBinomialTree,
};

}  // namespace numeraire
