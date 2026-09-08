#pragma once

#include <numeraire/core/imarket_data.hpp>
#include <numeraire/simulation/commodity_curve_resolver.hpp>
#include <numeraire/simulation/exposure_time_grid.hpp>
#include <numeraire/simulation/path_pricing_market_config.hpp>
#include <numeraire/simulation/scenario_buffer.hpp>

#include <cstddef>
#include <string>
#include <unordered_map>

namespace numeraire::simulation {

/// `IMarketData` view over one `(step, path)` slice of a multifactor `ScenarioBuffer`.
///
/// Spots come from simulated paths; IV and rates come from sticky DB quotes @ valuation `as_of`.
/// Call `SetSlice(step, path)` before pricing — the object is reused across the hot loop.
class ScenarioSliceMarketData final : public core::IMarketData {
   public:
    /// `commodity_curves` is optional: without it only calibration factor ids resolve,
    /// which is all an equity book needs. With it, dated futures tickers resolve too.
    ScenarioSliceMarketData(const ScenarioBuffer& buffer,
                            const ExposureTimeGrid& time_grid,
                            const std::unordered_map<std::string, std::size_t>& factor_by_underlying,
                            PathPricingMarketConfig market_config,
                            const CommodityCurveResolver* commodity_curves = nullptr);

    void SetSlice(std::size_t step, std::size_t path);

    [[nodiscard]] const schedule::Date& ValuationDate() const override;

    [[nodiscard]] double Spot(std::string_view underlying_id) const override;

    [[nodiscard]] double RiskFreeRate() const override;

    [[nodiscard]] double RiskFreeRateForTenor(double time_to_expiry_years) const override;

    [[nodiscard]] double DividendYield(std::string_view underlying_id) const override;

    [[nodiscard]] double ImpliedVolatility(std::string_view underlying_id,
                                           double strike,
                                           double time_to_expiry_years,
                                           OptionType option_kind) const override;

   private:
    const ScenarioBuffer& buffer_;
    const ExposureTimeGrid& time_grid_;
    const std::unordered_map<std::string, std::size_t>& factor_by_underlying_;
    PathPricingMarketConfig market_config_;
    const CommodityCurveResolver* commodity_curves_{nullptr};
    std::size_t step_{0};
    std::size_t path_{0};
};

}  // namespace numeraire::simulation
