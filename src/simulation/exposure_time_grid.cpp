#include <numeraire/simulation/exposure_time_grid.hpp>

#include <numeraire/schedule/date.hpp>
#include <numeraire/utils/exception.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace numeraire::simulation {
namespace {

constexpr double kDateCompareTol = 1.0e-10;

[[nodiscard]] bool DatesLess(const schedule::Date& a, const schedule::Date& b) {
    if (a.year != b.year) {
        return a.year < b.year;
    }
    if (a.month != b.month) {
        return a.month < b.month;
    }
    return a.day < b.day;
}

[[nodiscard]] bool DatesEqual(const schedule::Date& a, const schedule::Date& b) {
    return a.year == b.year && a.month == b.month && a.day == b.day;
}

[[nodiscard]] schedule::Date EffectiveHorizonEnd(const ExposureGridConfig& config,
                                                 const schedule::Date& valuation_date,
                                                 const std::optional<schedule::Date>& book_max_expiry) {
    int max_pillar_dte = 0;
    for (const ExposurePillarConfig& pillar : config.exposure_pillars) {
        max_pillar_dte = std::max(max_pillar_dte, pillar.target_dte_days);
    }
    const int horizon_dte = std::max(max_pillar_dte, config.conventions.min_horizon_days);
    schedule::Date horizon = schedule::AddCalendarDays(valuation_date, horizon_dte);

    if (config.conventions.clip_to_book_max_expiry && book_max_expiry.has_value()) {
        if (DatesLess(*book_max_expiry, horizon)) {
            horizon = *book_max_expiry;
        }
    }
    return horizon;
}

struct CandidateNode {
    schedule::Date date{};
    int target_dte_days{0};
    std::string pillar_id;
};

}  // namespace

ExposureTimeGrid BuildExposureTimeGrid(const ExposureGridConfig& config,
                                       const schedule::Date& valuation_date,
                                       const std::optional<schedule::Date>& book_max_expiry) {
    if (config.exposure_pillars.empty()) {
        throw ValidationError("BuildExposureTimeGrid: exposure_pillars must not be empty.");
    }

    const schedule::Date horizon_end =
            EffectiveHorizonEnd(config, valuation_date, book_max_expiry);

    std::vector<CandidateNode> candidates;
    candidates.reserve(config.exposure_pillars.size() + 1U);

    if (config.conventions.include_valuation_date) {
        candidates.push_back(CandidateNode{.date = valuation_date,
                                           .target_dte_days = 0,
                                           .pillar_id = "ASOF"});
    }

    for (const ExposurePillarConfig& pillar : config.exposure_pillars) {
        const schedule::Date node_date =
                schedule::AddCalendarDays(valuation_date, pillar.target_dte_days);
        if (DatesLess(node_date, valuation_date)) {
            continue;
        }
        if (DatesLess(horizon_end, node_date)) {
            continue;
        }
        candidates.push_back(CandidateNode{.date = node_date,
                                           .target_dte_days = pillar.target_dte_days,
                                           .pillar_id = pillar.id});
    }

    if (candidates.empty()) {
        candidates.push_back(CandidateNode{.date = valuation_date,
                                           .target_dte_days = 0,
                                           .pillar_id = "ASOF"});
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const CandidateNode& lhs, const CandidateNode& rhs) {
                  if (DatesEqual(lhs.date, rhs.date)) {
                      return lhs.target_dte_days < rhs.target_dte_days;
                  }
                  return DatesLess(lhs.date, rhs.date);
              });

    ExposureTimeGrid grid;
    grid.valuation_date = valuation_date;
    grid.nodes.reserve(candidates.size());

    for (const CandidateNode& candidate : candidates) {
        if (!grid.nodes.empty() && DatesEqual(grid.nodes.back().date, candidate.date)) {
            continue;
        }
        ExposureGridNode node;
        node.date = candidate.date;
        node.target_dte_days = candidate.target_dte_days;
        node.pillar_id = candidate.pillar_id;
        node.year_fraction = schedule::Act365FixedYearFraction(valuation_date, node.date);
        if (node.year_fraction < -kDateCompareTol) {
            throw ValidationError("BuildExposureTimeGrid: node precedes valuation_date.");
        }
        grid.nodes.push_back(std::move(node));
    }

    if (grid.nodes.empty()) {
        throw ValidationError("BuildExposureTimeGrid: produced empty grid.");
    }

    return grid;
}

}  // namespace numeraire::simulation
