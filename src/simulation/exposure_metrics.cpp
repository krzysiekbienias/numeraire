#include <numeraire/simulation/exposure_metrics.hpp>

#include <numeraire/schedule/format_iso_date.hpp>
#include <numeraire/utils/exception.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace numeraire::simulation {
namespace {

[[nodiscard]] double PositiveExposure(const double pv_total) noexcept {
    return pv_total > 0.0 ? pv_total : 0.0;
}

[[nodiscard]] double Mean(const std::vector<double>& values) {
    if (values.empty()) {
        return 0.0;
    }
    const double sum = std::accumulate(values.begin(), values.end(), 0.0);
    return sum / static_cast<double>(values.size());
}

[[nodiscard]] double Quantile(std::vector<double> values, const double q) {
    if (values.empty()) {
        return 0.0;
    }
    if (values.size() == 1U) {
        return values.front();
    }
    std::sort(values.begin(), values.end());
    const double clamped_q = std::clamp(q, 0.0, 1.0);
    const double pos = clamped_q * static_cast<double>(values.size() - 1U);
    const auto lo = static_cast<std::size_t>(std::floor(pos));
    const auto hi = static_cast<std::size_t>(std::ceil(pos));
    if (lo == hi) {
        return values[lo];
    }
    const double weight = pos - static_cast<double>(lo);
    return values[lo] * (1.0 - weight) + values[hi] * weight;
}

}  // namespace

void ComputeLegExposureMetrics(const LegPathPvBuffer& leg_pv,
                               const std::vector<LegExposureIdentity>& legs,
                               const ExposureTimeGrid& time_grid,
                               std::vector<LegExposureMetrics>& out_metrics) {
    if (legs.empty()) {
        throw ValidationError("ComputeLegExposureMetrics: legs must not be empty.");
    }
    if (leg_pv.NumLegs() != legs.size()) {
        throw ValidationError("ComputeLegExposureMetrics: leg_pv.NumLegs must match legs.size().");
    }
    if (leg_pv.NumSteps() != time_grid.NumSteps()) {
        throw ValidationError("ComputeLegExposureMetrics: leg_pv steps must match time_grid.NumSteps().");
    }
    if (time_grid.NumSteps() == 0U) {
        throw ValidationError("ComputeLegExposureMetrics: time_grid must not be empty.");
    }
    if (leg_pv.NumPaths() == 0U) {
        throw ValidationError("ComputeLegExposureMetrics: leg_pv must have at least one path.");
    }

    out_metrics.clear();
    out_metrics.reserve(legs.size() * time_grid.NumSteps());

    std::vector<double> exposures;
    exposures.reserve(leg_pv.NumPaths());

    for (std::size_t leg_index = 0; leg_index < legs.size(); ++leg_index) {
        const LegExposureIdentity& leg = legs[leg_index];
        for (std::size_t step = 0; step < time_grid.NumSteps(); ++step) {
            exposures.clear();
            for (std::size_t path = 0; path < leg_pv.NumPaths(); ++path) {
                exposures.push_back(PositiveExposure(leg_pv.At(leg_index, step, path)));
            }

            const ExposureGridNode& node = time_grid.nodes[step];
            LegExposureMetrics metrics{};
            metrics.leg_id = leg.leg_id;
            metrics.trade_id = leg.trade_id;
            metrics.pillar_id = node.pillar_id;
            metrics.grid_step = static_cast<int>(step);
            metrics.year_fraction = node.year_fraction;
            metrics.exposure_date = schedule::FormatIsoDate(node.date);
            metrics.ee = Mean(exposures);
            metrics.pfe_95 = Quantile(exposures, 0.95);
            metrics.pfe_97 = Quantile(exposures, 0.97);
            out_metrics.push_back(std::move(metrics));
        }
    }
}

}  // namespace numeraire::simulation
