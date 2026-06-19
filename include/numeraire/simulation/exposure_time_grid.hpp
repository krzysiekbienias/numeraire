#pragma once

#include <numeraire/schedule/date.hpp>
#include <numeraire/simulation/exposure_grid_config.hpp>

#include <optional>
#include <string>
#include <vector>

namespace numeraire::simulation {

/// One node on the exposure / simulation time grid (built at run time from a pattern).
struct ExposureGridNode {
    schedule::Date date{};
    /// Act/365 Fixed year fraction from `valuation_date` to `date`.
    double year_fraction{0.0};
    int target_dte_days{0};
    std::string pillar_id;
};

/// Concrete time grid for one valuation run.
struct ExposureTimeGrid {
    schedule::Date valuation_date{};
    std::vector<ExposureGridNode> nodes;

    [[nodiscard]] std::size_t NumSteps() const noexcept { return nodes.size(); }
};

/// Build exposure dates from a JSON pattern + valuation date. Optional `book_max_expiry`
/// clips the grid when `config.conventions.clip_to_book_max_expiry` is true. The effective
/// horizon is at least `min_horizon_days` after valuation when that convention is set.
[[nodiscard]] ExposureTimeGrid BuildExposureTimeGrid(const ExposureGridConfig& config,
                                                     const schedule::Date& valuation_date,
                                                     const std::optional<schedule::Date>& book_max_expiry =
                                                             std::nullopt);

}  // namespace numeraire::simulation
