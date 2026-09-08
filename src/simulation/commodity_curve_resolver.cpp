#include <numeraire/simulation/commodity_curve_resolver.hpp>

#include <numeraire/database/futures_pillar_returns.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/schedule/format_iso_date.hpp>
#include <numeraire/utils/exception.hpp>
#include <numeraire/utils/logger.hpp>

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace numeraire::simulation {
namespace {

/// A calibrated pillar reduced to what interpolation needs: how far out it sits and
/// which buffer row carries it.
struct PillarNode {
    double tenor_years{0.0};
    std::size_t factor_index{0};
};

/// Pillars of one product, ascending in maturity, restricted to those that actually
/// made it into the calibration factor set.
[[nodiscard]] std::vector<PillarNode> LoadCalibratedStrip(
        const std::string& database_file_path,
        const std::string& product_code,
        const std::unordered_map<std::string, std::size_t>& factor_by_underlying,
        const schedule::Date& as_of,
        const int num_pillars) {
    const std::string requested = schedule::FormatIsoDate(as_of);
    const database::FuturesPillarCurve curve = database::LoadLatestFuturesPillarCurve(
            database_file_path, product_code, requested, num_pillars);

    if (curve.occupants.empty()) {
        throw ValidationError("BuildCommodityCurveResolver: no calibrated pillars for product_code=" +
                              product_code + " on or before " + requested +
                              " (calibrate the book before pricing paths).");
    }

    if (curve.as_of != requested) {
        utils::Logger::NumInfo("Commodity pillar strip {} loaded @ {} (requested as_of={}).", product_code,
                               curve.as_of, requested);
    }

    const schedule::Date occupancy_date = schedule::ParseIsoDate(curve.as_of);
    std::vector<PillarNode> strip;
    strip.reserve(curve.occupants.size());
    for (const database::FuturesPillarOccupant& occupant : curve.occupants) {
        const auto it = factor_by_underlying.find(occupant.factor_id);
        if (it == factor_by_underlying.end()) {
            continue;
        }
        const double tenor =
                schedule::Act365FixedYearFraction(occupancy_date, schedule::ParseIsoDate(occupant.settlement_date));
        if (tenor <= 0.0) {
            continue;
        }
        strip.push_back(PillarNode{.tenor_years = tenor, .factor_index = it->second});
    }

    if (strip.empty()) {
        throw ValidationError("BuildCommodityCurveResolver: no calibrated pillars for product_code=" +
                              product_code + " on or before " + requested +
                              " (calibrate the book before pricing paths).");
    }
    std::sort(strip.begin(), strip.end(), [](const PillarNode& a, const PillarNode& b) {
        return a.tenor_years < b.tenor_years;
    });
    return strip;
}

/// Where a contract with `tenor_years` left to run sits on the strip.
[[nodiscard]] PillarBlend BlendAt(const std::vector<PillarNode>& strip, const double tenor_years) {
    // Nearer than the front pillar: the contract is in its final weeks and the front
    // month is the only thing left to drive it.
    if (tenor_years <= strip.front().tenor_years || strip.size() == 1U) {
        return PillarBlend{.lower_factor = strip.front().factor_index,
                           .upper_factor = strip.front().factor_index,
                           .upper_weight = 0.0};
    }
    if (tenor_years >= strip.back().tenor_years) {
        return PillarBlend{.lower_factor = strip.back().factor_index,
                           .upper_factor = strip.back().factor_index,
                           .upper_weight = 0.0};
    }

    std::size_t upper = 1U;
    while (upper + 1U < strip.size() && strip[upper].tenor_years < tenor_years) {
        ++upper;
    }
    const PillarNode& lo = strip[upper - 1U];
    const PillarNode& hi = strip[upper];
    const double span = hi.tenor_years - lo.tenor_years;
    const double weight = span > 0.0 ? (tenor_years - lo.tenor_years) / span : 0.0;
    return PillarBlend{.lower_factor = lo.factor_index,
                       .upper_factor = hi.factor_index,
                       .upper_weight = std::clamp(weight, 0.0, 1.0)};
}

}  // namespace

void CommodityCurveResolver::Add(std::string contract_ticker, std::vector<PillarBlend> blend_by_step) {
    by_ticker_.insert_or_assign(std::move(contract_ticker), std::move(blend_by_step));
}

const std::vector<PillarBlend>* CommodityCurveResolver::Find(const std::string_view contract_ticker) const {
    const auto it = by_ticker_.find(std::string(contract_ticker));
    return it == by_ticker_.end() ? nullptr : &it->second;
}

CommodityCurveResolver BuildCommodityCurveResolver(
        const std::string& database_file_path,
        const std::vector<CommodityContractRef>& contracts,
        const std::unordered_map<std::string, std::size_t>& factor_by_underlying,
        const ExposureTimeGrid& time_grid,
        const schedule::Date& as_of,
        const int num_pillars) {
    CommodityCurveResolver resolver;
    if (contracts.empty()) {
        return resolver;
    }
    if (time_grid.nodes.empty()) {
        throw ValidationError("BuildCommodityCurveResolver: time_grid must not be empty.");
    }

    std::map<std::string, std::vector<PillarNode>> strip_by_product;
    for (const CommodityContractRef& contract : contracts) {
        if (!strip_by_product.contains(contract.product_code)) {
            strip_by_product.emplace(contract.product_code,
                                     LoadCalibratedStrip(database_file_path, contract.product_code,
                                                         factor_by_underlying, as_of, num_pillars));
        }
        const std::vector<PillarNode>& strip = strip_by_product.at(contract.product_code);

        // Longest maturity is at the first node; if the strip covers that, it covers
        // every later node too.
        const double tenor_at_inception = schedule::Act365FixedYearFraction(as_of, contract.settlement_date);
        if (tenor_at_inception > strip.back().tenor_years) {
            throw ValidationError("BuildCommodityCurveResolver: contract " + contract.contract_ticker +
                                  " settles beyond the calibrated pillar strip for " + contract.product_code +
                                  " (raise --pillars and recalibrate).");
        }

        std::vector<PillarBlend> by_step;
        by_step.reserve(time_grid.nodes.size());
        for (const auto& node : time_grid.nodes) {
            by_step.push_back(BlendAt(strip, schedule::Act365FixedYearFraction(node.date,
                                                                               contract.settlement_date)));
        }
        resolver.Add(contract.contract_ticker, std::move(by_step));
    }
    return resolver;
}

}  // namespace numeraire::simulation
