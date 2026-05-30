#pragma once

#include <numeraire/database/discount_curve_eod_read.hpp>
#include <numeraire/schedule/date.hpp>

#include <optional>
#include <string>
#include <unordered_map>

namespace numeraire::market_data {

/// Immutable bundle of quotes fed into pricing (`IMarketData`).
struct MarketSnapshot {
    schedule::Date valuation_date{};  // as_of
    std::unordered_map<std::string, double> spots;
    double risk_free_rate{0.0};
    std::unordered_map<std::string, double> dividend_yields;
    /// Used for all `ImpliedVolatility` calls until a surface ships.
    double flat_implied_volatility{0.0};
    /// Bootstrapped discount curve; when set, pricing uses tenor-dependent zero rates.
    std::optional<database::DiscountCurveEodRead> discount_curve;
};

}  // namespace numeraire::market_data
