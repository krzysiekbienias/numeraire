#include <numeraire/simulation/leg_exposure_dump.hpp>

#include <numeraire/utils/exception.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <string>

namespace numeraire::simulation {
namespace {

void ValidateDumpInputs(const LegPathPvBuffer& leg_pv,
                        const ExposureTimeGrid& time_grid,
                        const std::vector<LegExposureIdentity>& legs,
                        const DumpLegExposurePathsOptions& options) {
    if (legs.empty()) {
        throw ValidationError("DumpLegExposurePathsCsv: legs must not be empty.");
    }
    if (leg_pv.NumLegs() != legs.size()) {
        throw ValidationError("DumpLegExposurePathsCsv: leg_pv.NumLegs must match legs.size().");
    }
    if (leg_pv.NumSteps() != time_grid.NumSteps()) {
        throw ValidationError("DumpLegExposurePathsCsv: leg_pv steps must match time_grid.NumSteps().");
    }
    if (time_grid.NumSteps() == 0U) {
        throw ValidationError("DumpLegExposurePathsCsv: time_grid must not be empty.");
    }
    if (options.max_paths == 0U) {
        throw ValidationError("DumpLegExposurePathsCsv: max_paths must be > 0.");
    }
}

[[nodiscard]] int EnvIntOrDefault(const char* key, const int default_value) {
    const char* raw = std::getenv(key);
    if (raw == nullptr || raw[0] == '\0') {
        return default_value;
    }
    char* end = nullptr;
    const long v = std::strtol(raw, &end, 10);
    if (end == raw) {
        return default_value;
    }
    return static_cast<int>(v);
}

[[nodiscard]] std::size_t ResolvePathsToWrite(const std::size_t buffer_num_paths,
                                              const std::size_t requested_max_paths,
                                              const int env_cap) {
    if (env_cap > 0) {
        return std::min(buffer_num_paths, static_cast<std::size_t>(env_cap));
    }
    if (requested_max_paths > 0U) {
        return std::min(buffer_num_paths, requested_max_paths);
    }
    return buffer_num_paths;
}

[[nodiscard]] double PositiveExposure(const double pv_total) noexcept {
    return pv_total > 0.0 ? pv_total : 0.0;
}

}  // namespace

void DumpLegExposurePathsCsv(const std::filesystem::path& output_path,
                             const LegPathPvBuffer& leg_pv,
                             const ExposureTimeGrid& time_grid,
                             const std::vector<LegExposureIdentity>& legs,
                             const DumpLegExposurePathsOptions& options) {
    ValidateDumpInputs(leg_pv, time_grid, legs, options);

    const std::size_t paths_to_write = std::min(leg_pv.NumPaths(), options.max_paths);

    std::ofstream out(output_path);
    if (!out) {
        throw ValidationError("DumpLegExposurePathsCsv: failed to open output path: " + output_path.string());
    }

    out << std::setprecision(17);
    out << "path,leg_id,trade_id,pillar_id,step,year_fraction,pv_total,exposure\n";
    for (std::size_t path = 0; path < paths_to_write; ++path) {
        for (std::size_t leg_index = 0; leg_index < legs.size(); ++leg_index) {
            const LegExposureIdentity& leg = legs[leg_index];
            for (std::size_t step = 0; step < time_grid.NumSteps(); ++step) {
                const double pv_total = leg_pv.At(leg_index, step, path);
                out << path << ',' << leg.leg_id << ',' << leg.trade_id << ','
                    << time_grid.nodes[step].pillar_id << ',' << step << ','
                    << time_grid.nodes[step].year_fraction << ',' << pv_total << ','
                    << PositiveExposure(pv_total) << '\n';
            }
        }
    }
}

bool DumpLegExposurePathsIfEnvSet(const LegPathPvBuffer& leg_pv,
                                  const ExposureTimeGrid& time_grid,
                                  const std::vector<LegExposureIdentity>& legs,
                                  const DumpLegExposurePathsOptions& options) {
    const char* raw = std::getenv(kDumpLegExposureEnvVar);
    if (raw == nullptr || raw[0] == '\0') {
        return false;
    }
    DumpLegExposurePathsOptions resolved = options;
    resolved.max_paths = ResolvePathsToWrite(leg_pv.NumPaths(), options.max_paths,
                                             EnvIntOrDefault(kDumpLegExposureMaxPathsEnvVar, 0));
    DumpLegExposurePathsCsv(std::filesystem::path(raw), leg_pv, time_grid, legs, resolved);
    return true;
}

}  // namespace numeraire::simulation
