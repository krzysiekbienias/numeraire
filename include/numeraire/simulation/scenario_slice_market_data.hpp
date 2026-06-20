#pragma once

#include <numeraire/core/imarket_data.hpp>
#include <numeraire/simulation/exposure_time_grid.hpp>
#include <numeraire/simulation/path_pricing_quotes.hpp>
#include <numeraire/simulation/scenario_buffer.hpp>

#include <cstddef>
#include <string>
#include <unordered_map>

namespace numeraire::simulation {

/// `IMarketData` view over one `(step, path)` slice of a multifactor `ScenarioBuffer`.
///
/// Spots come from simulated paths; `r`, `q`, and flat vol are held constant (v1).
/// Call `SetSlice(step, path)` before pricing — the object is reused across the hot loop.
class ScenarioSliceMarketData final : public core::IMarketData {
   public:
    ScenarioSliceMarketData(const ScenarioBuffer& buffer,
                            const ExposureTimeGrid& time_grid,
                            const std::unordered_map<std::string, std::size_t>& factor_by_underlying,
                            PathPricingQuotes quotes);

    void SetSlice(std::size_t step, std::size_t path);

    [[nodiscard]] const schedule::Date& ValuationDate() const override;

    [[nodiscard]] double Spot(std::string_view underlying_id) const override;

    [[nodiscard]] double RiskFreeRate() const override;

    [[nodiscard]] double DividendYield(std::string_view underlying_id) const override;

    [[nodiscard]] double ImpliedVolatility(std::string_view underlying_id,
                                           double strike,
                                           double time_to_expiry_years,
                                           OptionType option_kind) const override;

   private:
    const ScenarioBuffer& buffer_;
    const ExposureTimeGrid& time_grid_;
    const std::unordered_map<std::string, std::size_t>& factor_by_underlying_;
    PathPricingQuotes quotes_;
    std::size_t step_{0};
    std::size_t path_{0};
};

}  // namespace numeraire::simulation
