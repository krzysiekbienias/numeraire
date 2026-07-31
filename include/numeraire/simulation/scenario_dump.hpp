#pragma once

#include <numeraire/simulation/exposure_time_grid.hpp>
#include <numeraire/simulation/scenario_buffer.hpp>

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>

namespace numeraire::simulation {

/// Environment variable: when set to a non-empty file path, scenario paths are written as CSV.
inline constexpr const char* kDumpScenarioPathsEnvVar = "NUMERAIRE_DUMP_SCENARIOS";
/// Optional cap on exported paths (omit or <=0 = dump every simulated path; viz subsamples in Python).
inline constexpr const char* kDumpScenarioPathsMaxPathsEnvVar = "NUMERAIRE_DUMP_SCENARIOS_MAX_PATHS";

struct DumpScenarioPathsOptions {
    std::size_t factor = 0;
    /// Paths written; use `buffer.NumPaths()` for a full run (default when env cap is unset).
    std::size_t max_paths = 0;
};

struct DumpMultiFactorScenarioPathsOptions {
    /// Paths written; use `buffer.NumPaths()` for a full run (default when env cap is unset).
    std::size_t max_paths = 0;
};

/// Write long-form CSV for one factor: `path,step,year_fraction,value`.
void DumpScenarioPathsCsv(const std::filesystem::path& output_path, const ScenarioBuffer& buffer,
                          const ExposureTimeGrid& time_grid,
                          const DumpScenarioPathsOptions& options = {});

/// Write long-form CSV for all factors:
/// `path,factor,underlying_id,step,year_fraction,value`.
void DumpMultiFactorScenarioPathsCsv(const std::filesystem::path& output_path,
                                     const ScenarioBuffer& buffer,
                                     const ExposureTimeGrid& time_grid,
                                     std::span<const std::string> underlying_ids,
                                     const DumpMultiFactorScenarioPathsOptions& options = {});

/// Dump single-factor CSV to `NUMERAIRE_DUMP_SCENARIOS` when set.
[[nodiscard]] bool DumpScenarioPathsIfEnvSet(const ScenarioBuffer& buffer,
                                             const ExposureTimeGrid& time_grid,
                                             const DumpScenarioPathsOptions& options = {});

/// Dump all factors to `NUMERAIRE_DUMP_SCENARIOS` when set.
/// Writes all simulated paths unless `NUMERAIRE_DUMP_SCENARIOS_MAX_PATHS` is set.
[[nodiscard]] bool DumpMultiFactorScenarioPathsIfEnvSet(
        const ScenarioBuffer& buffer,
        const ExposureTimeGrid& time_grid,
        std::span<const std::string> underlying_ids,
        const DumpMultiFactorScenarioPathsOptions& options = {});

}  // namespace numeraire::simulation
