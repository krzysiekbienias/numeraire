#pragma once

#include <numeraire/schedule/date.hpp>
#include <numeraire/simulation/exposure_time_grid.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace numeraire::simulation {

/// Log-linear blend of two calibrated pillar factors standing in for one dated
/// contract at one grid node.
struct PillarBlend {
    std::size_t lower_factor{0};
    std::size_t upper_factor{0};
    /// Weight on `upper_factor` in log space; 0 collapses onto `lower_factor`.
    double upper_weight{0.0};
};

/// Locates dated futures contracts on a calibrated constant-maturity pillar strip.
///
/// Calibration produces factors like `CL_M1..CL_M6`, but a booked leg references a
/// dated contract such as `CLX6`. The two live in different id spaces, and the gap
/// widens as the exposure grid marches forward: the contract keeps its settlement
/// date while the pillars keep their maturities, so the contract rolls down the
/// strip. The blend is therefore resolved per grid node rather than fixed once.
///
/// Weights depend on dates alone, so a single table serves every path — resolving a
/// spot is two buffer reads and an interpolation.
class CommodityCurveResolver {
   public:
    /// `blend_by_step` must hold one entry per exposure grid node.
    void Add(std::string contract_ticker, std::vector<PillarBlend> blend_by_step);

    [[nodiscard]] const std::vector<PillarBlend>* Find(std::string_view contract_ticker) const;

    [[nodiscard]] bool Empty() const { return by_ticker_.empty(); }

   private:
    std::unordered_map<std::string, std::vector<PillarBlend>> by_ticker_;
};

/// A dated contract whose position on the strip has to be tracked.
struct CommodityContractRef {
    std::string contract_ticker;
    std::string product_code;
    schedule::Date settlement_date{};
};

/// Build the per-node blend table for `contracts` against the calibrated factor set.
///
/// `as_of` is the calibration / curve date, not necessarily the valuation date.
/// Occupancy is the latest quoted session on or before that date, so a monthly
/// calibration still prices on later days without a new settle or a new snapshot.
/// Pillar maturities are read off that occupancy session: when it coincides with
/// inception, every listed contract *is* a pillar occupant, so node zero reproduces
/// its own settle rather than an interpolation. Throws when a contract matures
/// beyond the deepest calibrated pillar, since silently clamping would price a
/// deferred leg with front-month dynamics.
[[nodiscard]] CommodityCurveResolver BuildCommodityCurveResolver(
        const std::string& database_file_path,
        const std::vector<CommodityContractRef>& contracts,
        const std::unordered_map<std::string, std::size_t>& factor_by_underlying,
        const ExposureTimeGrid& time_grid,
        const schedule::Date& as_of,
        int num_pillars = 6);

}  // namespace numeraire::simulation
