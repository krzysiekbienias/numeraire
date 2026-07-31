#include <numeraire/simulation/scenario_slice_market_data.hpp>

#include <numeraire/market_data/discount_curve_rate.hpp>
#include <numeraire/market_data/vol_surface_interpolation.hpp>
#include <numeraire/utils/exception.hpp>

#include <cmath>
#include <string>

namespace numeraire::simulation {

ScenarioSliceMarketData::ScenarioSliceMarketData(
        const ScenarioBuffer& buffer,
        const ExposureTimeGrid& time_grid,
        const std::unordered_map<std::string, std::size_t>& factor_by_underlying,
        PathPricingMarketConfig market_config)
    : buffer_(buffer),
      time_grid_(time_grid),
      factor_by_underlying_(factor_by_underlying),
      market_config_(std::move(market_config)) {
    if (buffer.NumSteps() != time_grid.NumSteps()) {
        throw ValidationError("ScenarioSliceMarketData: buffer steps must match time_grid.NumSteps().");
    }
    if (time_grid.NumSteps() == 0U) {
        throw ValidationError("ScenarioSliceMarketData: time_grid must not be empty.");
    }
}

void ScenarioSliceMarketData::SetSlice(const std::size_t step, const std::size_t path) {
    if (step >= buffer_.NumSteps()) {
        throw ValidationError("ScenarioSliceMarketData: step out of range.");
    }
    if (path >= buffer_.NumPaths()) {
        throw ValidationError("ScenarioSliceMarketData: path out of range.");
    }
    step_ = step;
    path_ = path;
}

const schedule::Date& ScenarioSliceMarketData::ValuationDate() const {
    return time_grid_.nodes[step_].date;
}

double ScenarioSliceMarketData::Spot(const std::string_view underlying_id) const {
    const std::string key(underlying_id);
    const auto it = factor_by_underlying_.find(key);
    if (it == factor_by_underlying_.end()) {
        throw MarketDataError("ScenarioSliceMarketData: unknown underlying \"" + key +
                              "\" (not in calibration factor set).");
    }
    return buffer_.At(it->second, step_, path_);
}

double ScenarioSliceMarketData::RiskFreeRate() const {
    return numeraire::market_data::RepresentativeRiskFreeRate(market_config_.discount_curve,
                                                              market_config_.flat_fallbacks.risk_free_rate);
}

double ScenarioSliceMarketData::RiskFreeRateForTenor(const double time_to_expiry_years) const {
    return numeraire::market_data::RiskFreeRateForTenor(market_config_.discount_curve,
                                                        market_config_.flat_fallbacks.risk_free_rate,
                                                        time_to_expiry_years);
}

double ScenarioSliceMarketData::DividendYield(const std::string_view underlying_id) const {
    const std::string key(underlying_id);
    const auto it = market_config_.dividend_yields.find(key);
    if (it == market_config_.dividend_yields.end()) {
        return market_config_.flat_fallbacks.dividend_yield;
    }
    return it->second;
}

double ScenarioSliceMarketData::ImpliedVolatility(const std::string_view underlying_id,
                                                  const double strike,
                                                  const double time_to_expiry_years,
                                                  const OptionType option_kind) const {
    if (strike <= 0.0) {
        throw MarketDataError("ScenarioSliceMarketData::ImpliedVolatility: strike must be positive");
    }
    if (time_to_expiry_years <= 0.0) {
        return 0.0;
    }

    const auto surface_it = market_config_.vol_surfaces.find(std::string(underlying_id));
    if (surface_it == market_config_.vol_surfaces.end()) {
        return market_config_.flat_fallbacks.flat_implied_volatility;
    }

    const database::VolSurfaceEodRead& surface = surface_it->second;
    const double spot = Spot(underlying_id);
    if (spot <= 0.0) {
        throw MarketDataError("ScenarioSliceMarketData::ImpliedVolatility: spot must be positive for \"" +
                              std::string(underlying_id) + "\"");
    }

    const double ln_m = std::log(strike / spot);
    const auto& points = option_kind == OptionType::kCall ? surface.call_points : surface.put_points;
    if (points.empty()) {
        throw MarketDataError("ScenarioSliceMarketData::ImpliedVolatility: empty " +
                              std::string(option_kind == OptionType::kCall ? "call" : "put") + " surface for \"" +
                              std::string(underlying_id) + "\"");
    }

    return numeraire::market_data::InterpolateImpliedVol(points, ln_m, time_to_expiry_years);
}

}  // namespace numeraire::simulation
