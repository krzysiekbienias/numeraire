#pragma once

#include <string_view>

namespace numeraire::simulation {

/// Walking-skeleton identity for the simulation module. The SoA Monte Carlo
/// engine (scenario buffer, RNG, GBM kernel, path pricing) is built on top of
/// this in later steps; for now it gives the library a single compiled symbol so
/// the module and its test target build and link end-to-end.
[[nodiscard]] std::string_view ModuleName() noexcept;

}  // namespace numeraire::simulation
