#pragma once

#include <numeraire/simulation/exposure_time_grid.hpp>
#include <numeraire/simulation/scenario_buffer.hpp>

#include <cstddef>
#include <filesystem>

namespace numeraire::simulation {

/// Environment variable: when set to a non-empty file path, scenario paths are written as CSV.
inline constexpr const char* kDumpScenarioPathsEnvVar = "NUMERAIRE_DUMP_SCENARIOS";

struct DumpScenarioPathsOptions {
    std::size_t factor = 0;
    /// Cap paths written (simulation may use 1000+; dump only a sample for offline viz).
    std::size_t max_paths = 32;
};

/// Write long-form CSV: `path,step,year_fraction,value`.
///
/// Only the first `max_paths` Monte Carlo paths are exported. All time steps from
/// `time_grid` are included for each exported path.
void DumpScenarioPathsCsv(const std::filesystem::path& output_path, const ScenarioBuffer& buffer,
                          const ExposureTimeGrid& time_grid,
                          const DumpScenarioPathsOptions& options = {});

/// Dump to the path in `NUMERAIRE_DUMP_SCENARIOS` when that variable is set.
[[nodiscard]] bool DumpScenarioPathsIfEnvSet(const ScenarioBuffer& buffer,
                                             const ExposureTimeGrid& time_grid,
                                             const DumpScenarioPathsOptions& options = {});

}  // namespace numeraire::simulation
