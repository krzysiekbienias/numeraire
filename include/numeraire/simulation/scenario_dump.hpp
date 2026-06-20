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
/// Cap exported Monte Carlo paths when dumping (default used by multifactor simulate export).
inline constexpr const char* kDumpScenarioPathsMaxPathsEnvVar = "NUMERAIRE_DUMP_SCENARIOS_MAX_PATHS";

struct DumpScenarioPathsOptions {
    std::size_t factor = 0;
    /// Cap paths written (simulation may use 1000+; dump only a sample for offline viz).
    std::size_t max_paths = 32;
};

struct DumpMultiFactorScenarioPathsOptions {
    /// Cap paths written per underlying (viz sample).
    std::size_t max_paths = 10;
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
/// Honors `NUMERAIRE_DUMP_SCENARIOS_MAX_PATHS` (default 10).
[[nodiscard]] bool DumpMultiFactorScenarioPathsIfEnvSet(
        const ScenarioBuffer& buffer,
        const ExposureTimeGrid& time_grid,
        std::span<const std::string> underlying_ids,
        const DumpMultiFactorScenarioPathsOptions& options = {});

}  // namespace numeraire::simulation
