#pragma once

#include <numeraire/simulation/exposure_time_grid.hpp>
#include <numeraire/simulation/leg_path_pv_buffer.hpp>

#include <string>
#include <vector>

namespace numeraire::simulation {

inline constexpr const char* kPathExposurePricingEngine = "analytic_bs_path";

/// Leg identity for exposure aggregation (no product handle).
struct LegExposureIdentity {
    std::string leg_id;
    std::string trade_id;
};

/// Aggregated MC exposure metrics for one leg at one exposure-grid pillar.
struct LegExposureMetrics {
    std::string leg_id;
    std::string trade_id;
    std::string pillar_id;
    int grid_step{0};
    double year_fraction{};
    std::string exposure_date;
    double ee{};
    double pfe_95{};
    double pfe_97{};
};

/// Compute EE (mean) and PFE quantiles from path-wise leg PV totals.
/// Exposure per path: `max(0, pv_total)`.
void ComputeLegExposureMetrics(const LegPathPvBuffer& leg_pv,
                               const std::vector<LegExposureIdentity>& legs,
                               const ExposureTimeGrid& time_grid,
                               std::vector<LegExposureMetrics>& out_metrics);

}  // namespace numeraire::simulation
