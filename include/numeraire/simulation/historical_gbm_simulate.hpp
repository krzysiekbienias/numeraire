#pragma once

#include <numeraire/utils/config.hpp>

namespace numeraire::simulation {

void PrintHistoricalGbmSimulateUsageLines();

/// Returns exit code if `--simulate` was requested; `-1` otherwise.
[[nodiscard]] int TryRunHistoricalGbmSimulate(int argc, char** argv, const numeraire::utils::Config& cfg);

}  // namespace numeraire::simulation
