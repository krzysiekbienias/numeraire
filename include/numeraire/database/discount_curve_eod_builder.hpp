#pragma once

#include <numeraire/database/discount_curve_types.hpp>
#include <numeraire/utils/config.hpp>

#include <string>

namespace numeraire::database {

struct DiscountCurveBuildParams {
    std::string database_file_path;
    std::string curve_id;
    std::string as_of;
    std::string batch_run_id;
};

/// Loads par-yield quotes from SQLite, bootstraps discount factors, persists `discount_curve_*`.
[[nodiscard]] DiscountCurveBuildStats BuildDiscountCurveEod(const DiscountCurveBuildParams& params);

void PrintDiscountCurveEodBuildUsageLines();

/// Returns exit code if `--build-discount-curve-eod` was requested; `-1` otherwise.
[[nodiscard]] int TryRunDiscountCurveEodBuild(int argc, char** argv, const numeraire::utils::Config& cfg);

}  // namespace numeraire::database
