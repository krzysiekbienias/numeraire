#pragma once

#include <numeraire/simulation/exposure_metrics.hpp>
#include <numeraire/simulation/exposure_time_grid.hpp>
#include <numeraire/simulation/leg_path_pv_buffer.hpp>

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace numeraire::simulation {

inline constexpr const char* kDumpLegExposureEnvVar = "NUMERAIRE_DUMP_LEG_EXPOSURE";
inline constexpr const char* kDumpLegExposureMaxPathsEnvVar = "NUMERAIRE_DUMP_LEG_EXPOSURE_MAX_PATHS";

struct DumpLegExposurePathsOptions {
    /// Paths written; use `leg_pv.NumPaths()` for a full run (default when env cap is unset).
    std::size_t max_paths = 0;
};

/// Write long-form CSV: `path,leg_id,trade_id,pillar_id,step,year_fraction,pv_total,exposure`.
void DumpLegExposurePathsCsv(const std::filesystem::path& output_path,
                             const LegPathPvBuffer& leg_pv,
                             const ExposureTimeGrid& time_grid,
                             const std::vector<LegExposureIdentity>& legs,
                             const DumpLegExposurePathsOptions& options = {});

/// Dump when `NUMERAIRE_DUMP_LEG_EXPOSURE` is set.
/// Writes all simulated paths unless `NUMERAIRE_DUMP_LEG_EXPOSURE_MAX_PATHS` is set.
[[nodiscard]] bool DumpLegExposurePathsIfEnvSet(const LegPathPvBuffer& leg_pv,
                                                  const ExposureTimeGrid& time_grid,
                                                  const std::vector<LegExposureIdentity>& legs,
                                                  const DumpLegExposurePathsOptions& options = {});

}  // namespace numeraire::simulation
