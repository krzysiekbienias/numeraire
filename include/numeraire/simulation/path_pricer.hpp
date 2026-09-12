#pragma once

#include <numeraire/core/ipricer.hpp>
#include <numeraire/core/iproduct.hpp>
#include <numeraire/enums/position_direction.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/simulation/commodity_curve_resolver.hpp>
#include <numeraire/simulation/exposure_time_grid.hpp>
#include <numeraire/simulation/leg_path_pv_buffer.hpp>
#include <numeraire/simulation/path_pricing_market_config.hpp>
#include <numeraire/simulation/scenario_buffer.hpp>

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace numeraire::simulation {

/// One LIVE leg prepared for path-wise repricing.
struct PathPricingLegEntry {
    std::string leg_id;
    std::string trade_id;
    std::string underlying_id;
    std::size_t factor_index{0};
    numeraire::PositionDirection direction{};
    double quantity{0.0};
    double contract_size{0.0};
    schedule::Date expiry_date{};
    std::unique_ptr<core::IProduct> product;
    /// Set for listed futures legs: the dated contract this leg tracks.
    std::optional<CommodityContractRef> contract;
    /// Subtracted from the unit PV before scaling. Zero for options and for
    /// commodity/equity forwards, whose K (or premium) is already in PV. For a
    /// listed futures outright it is the trade price, because the EOD pricer
    /// marks at full settle and exposure should be the move since execution.
    double pv_unit_offset{0.0};
};

/// Map calibration factor order (`factor_ids[f]`) to buffer factor indices.
[[nodiscard]] std::unordered_map<std::string, std::size_t> BuildFactorIndexByUnderlying(
        std::span<const std::string> factor_underlying_ids);

/// Load LIVE legs for `portfolio_id` (run `ApplyTradeLifecycleAsOf` before this on the same `as_of`).
[[nodiscard]] std::vector<PathPricingLegEntry> LoadPathPricingLegsForPortfolio(
        const std::string& database_file_path,
        const std::string& portfolio_id,
        const std::unordered_map<std::string, std::size_t>& factor_by_underlying);

/// Dated contracts referenced by `legs`, ready for `BuildCommodityCurveResolver`.
[[nodiscard]] std::vector<CommodityContractRef> CollectCommodityContracts(
        const std::vector<PathPricingLegEntry>& legs);

/// Price every leg at every `(step, path)` into `out_pv` (shape must match buffer/grid/legs).
/// `commodity_curves` is required whenever any leg carries a `contract`.
void PricePortfolioAlongPaths(const ScenarioBuffer& buffer,
                              const ExposureTimeGrid& time_grid,
                              std::span<const std::string> factor_underlying_ids,
                              const std::vector<PathPricingLegEntry>& legs,
                              const PathPricingMarketConfig& market_config,
                              const core::IPricer& pricer,
                              LegPathPvBuffer& out_pv,
                              std::vector<std::string>& out_leg_ids,
                              const CommodityCurveResolver* commodity_curves = nullptr);

}  // namespace numeraire::simulation
