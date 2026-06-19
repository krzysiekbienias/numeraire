#include <numeraire/simulation/scenario_dump.hpp>

#include <numeraire/utils/exception.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <string>

namespace numeraire::simulation {
namespace {

void ValidateDumpInputs(const ScenarioBuffer& buffer, const ExposureTimeGrid& time_grid,
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

}  // namespace

void DumpScenarioPathsCsv(const std::filesystem::path& output_path, const ScenarioBuffer& buffer,
                          const ExposureTimeGrid& time_grid, const DumpScenarioPathsOptions& options) {
    ValidateDumpInputs(buffer, time_grid, options);

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

bool DumpScenarioPathsIfEnvSet(const ScenarioBuffer& buffer, const ExposureTimeGrid& time_grid,
                               const DumpScenarioPathsOptions& options) {
    const char* raw = std::getenv(kDumpScenarioPathsEnvVar);
    if (raw == nullptr || raw[0] == '\0') {
        return false;
    }
    DumpScenarioPathsCsv(std::filesystem::path(raw), buffer, time_grid, options);
    return true;
}

}  // namespace numeraire::simulation
