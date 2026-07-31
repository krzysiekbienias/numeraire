#pragma once

#include <numeraire/database/vol_surface_types.hpp>
#include <numeraire/utils/config.hpp>

#include <string>

namespace numeraire::database {

struct VolSurfaceBuildParams {
    std::string database_file_path;
    std::string underlying_id;
    std::string index_ticker;
    std::string as_of;
    /// Flat fallback when no discount curve is available for `as_of`.
    double risk_free_rate{0.03};
    double dividend_yield{0.0};
    /// Curve for per-expiry zero rates used in IV inversion (latest `as_of` ≤ mark).
    std::string discount_curve_id{"USD_TREASURY_PAR_FRED"};
    int adjusted{1};
    std::string surface_kind{"implied_bs_eod"};
    std::string batch_run_id;
};

/// Loads option quotes from SQLite, inverts IV in `numeraire::quant`, persists surface tables.
[[nodiscard]] VolSurfaceBuildStats BuildVolSurfaceEod(const VolSurfaceBuildParams& params);

void PrintVolSurfaceEodBuildUsageLines();

/// Returns exit code if `--build-vol-surface-eod` was requested; `-1` otherwise.
[[nodiscard]] int TryRunVolSurfaceEodBuild(int argc, char** argv, const numeraire::utils::Config& cfg);

}  // namespace numeraire::database
