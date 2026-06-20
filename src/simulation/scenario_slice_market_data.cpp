#include <numeraire/simulation/scenario_slice_market_data.hpp>

#include <numeraire/utils/exception.hpp>

#include <string>

namespace numeraire::simulation {

ScenarioSliceMarketData::ScenarioSliceMarketData(
        const ScenarioBuffer& buffer,
        const ExposureTimeGrid& time_grid,
        const std::unordered_map<std::string, std::size_t>& factor_by_underlying,
        const PathPricingQuotes quotes)
    : buffer_(buffer),
      time_grid_(time_grid),
      factor_by_underlying_(factor_by_underlying),
      quotes_(quotes) {
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
    return quotes_.risk_free_rate;
}

double ScenarioSliceMarketData::DividendYield(const std::string_view underlying_id) const {
    static_cast<void>(underlying_id);
    return quotes_.dividend_yield;
}

double ScenarioSliceMarketData::ImpliedVolatility(const std::string_view underlying_id,
                                                  const double strike,
                                                  const double time_to_expiry_years,
                                                  const OptionType option_kind) const {
    static_cast<void>(underlying_id);
    static_cast<void>(strike);
    static_cast<void>(time_to_expiry_years);
    static_cast<void>(option_kind);
    return quotes_.flat_implied_volatility;
}

}  // namespace numeraire::simulation
