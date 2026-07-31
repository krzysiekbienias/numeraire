#include <numeraire/simulation/scenario_dump.hpp>

#include <numeraire/utils/exception.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <string>

namespace numeraire::simulation {
namespace {

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

void ValidateSingleFactorDumpInputs(const ScenarioBuffer& buffer, const ExposureTimeGrid& time_grid,
                                    const DumpScenarioPathsOptions& options) {
    if (buffer.NumSteps() != time_grid.NumSteps()) {
        throw ValidationError("DumpScenarioPathsCsv: buffer steps must match time_grid.NumSteps().");
    }
    if (time_grid.NumSteps() == 0U) {
        throw ValidationError("DumpScenarioPathsCsv: time_grid must not be empty.");
    }
    if (options.factor >= buffer.NumFactors()) {
        throw ValidationError("DumpScenarioPathsCsv: factor out of range.");
    }
    if (options.max_paths == 0U) {
        throw ValidationError("DumpScenarioPathsCsv: max_paths must be > 0.");
    }
}

void ValidateMultiFactorDumpInputs(const ScenarioBuffer& buffer, const ExposureTimeGrid& time_grid,
                                   const std::span<const std::string> underlying_ids,
                                   const DumpMultiFactorScenarioPathsOptions& options) {
    if (buffer.NumSteps() != time_grid.NumSteps()) {
        throw ValidationError(
                "DumpMultiFactorScenarioPathsCsv: buffer steps must match time_grid.NumSteps().");
    }
    if (time_grid.NumSteps() == 0U) {
        throw ValidationError("DumpMultiFactorScenarioPathsCsv: time_grid must not be empty.");
    }
    if (underlying_ids.size() != buffer.NumFactors()) {
        throw ValidationError(
                "DumpMultiFactorScenarioPathsCsv: underlying_ids length must match NumFactors().");
    }
    if (options.max_paths == 0U) {
        throw ValidationError("DumpMultiFactorScenarioPathsCsv: max_paths must be > 0.");
    }
}

}  // namespace

void DumpScenarioPathsCsv(const std::filesystem::path& output_path, const ScenarioBuffer& buffer,
                          const ExposureTimeGrid& time_grid, const DumpScenarioPathsOptions& options) {
    ValidateSingleFactorDumpInputs(buffer, time_grid, options);

    const std::size_t paths_to_write = std::min(buffer.NumPaths(), options.max_paths);

    std::ofstream out(output_path);
    if (!out) {
        throw ValidationError("DumpScenarioPathsCsv: failed to open output path: " +
                              output_path.string());
    }

    out << std::setprecision(17);
    out << "path,step,year_fraction,value\n";
    for (std::size_t path = 0; path < paths_to_write; ++path) {
        for (std::size_t step = 0; step < time_grid.NumSteps(); ++step) {
            out << path << ',' << step << ',' << time_grid.nodes[step].year_fraction << ','
                << buffer.At(options.factor, step, path) << '\n';
        }
    }
}

void DumpMultiFactorScenarioPathsCsv(const std::filesystem::path& output_path,
                                     const ScenarioBuffer& buffer,
                                     const ExposureTimeGrid& time_grid,
                                     const std::span<const std::string> underlying_ids,
                                     const DumpMultiFactorScenarioPathsOptions& options) {
    ValidateMultiFactorDumpInputs(buffer, time_grid, underlying_ids, options);

    const std::size_t paths_to_write = std::min(buffer.NumPaths(), options.max_paths);

    std::ofstream out(output_path);
    if (!out) {
        throw ValidationError("DumpMultiFactorScenarioPathsCsv: failed to open output path: " +
                              output_path.string());
    }

    out << std::setprecision(17);
    out << "path,factor,underlying_id,step,year_fraction,value\n";
    for (std::size_t path = 0; path < paths_to_write; ++path) {
        for (std::size_t factor = 0; factor < buffer.NumFactors(); ++factor) {
            for (std::size_t step = 0; step < time_grid.NumSteps(); ++step) {
                out << path << ',' << factor << ',' << underlying_ids[factor] << ',' << step << ','
                    << time_grid.nodes[step].year_fraction << ','
                    << buffer.At(factor, step, path) << '\n';
            }
        }
    }
}

bool DumpScenarioPathsIfEnvSet(const ScenarioBuffer& buffer, const ExposureTimeGrid& time_grid,
                               const DumpScenarioPathsOptions& options) {
    const char* raw = std::getenv(kDumpScenarioPathsEnvVar);
    if (raw == nullptr || raw[0] == '\0') {
        return false;
    }
    DumpScenarioPathsOptions resolved = options;
    resolved.max_paths = ResolvePathsToWrite(buffer.NumPaths(), options.max_paths,
                                             EnvIntOrDefault(kDumpScenarioPathsMaxPathsEnvVar, 0));
    DumpScenarioPathsCsv(std::filesystem::path(raw), buffer, time_grid, resolved);
    return true;
}

bool DumpMultiFactorScenarioPathsIfEnvSet(const ScenarioBuffer& buffer,
                                            const ExposureTimeGrid& time_grid,
                                            const std::span<const std::string> underlying_ids,
                                            const DumpMultiFactorScenarioPathsOptions& options) {
    const char* raw = std::getenv(kDumpScenarioPathsEnvVar);
    if (raw == nullptr || raw[0] == '\0') {
        return false;
    }
    DumpMultiFactorScenarioPathsOptions resolved = options;
    resolved.max_paths = ResolvePathsToWrite(buffer.NumPaths(), options.max_paths,
                                             EnvIntOrDefault(kDumpScenarioPathsMaxPathsEnvVar, 0));
    DumpMultiFactorScenarioPathsCsv(std::filesystem::path(raw), buffer, time_grid, underlying_ids,
                                    resolved);
    return true;
}

}  // namespace numeraire::simulation
