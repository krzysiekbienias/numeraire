#pragma once

#include <string>
#include <unordered_map>

namespace numeraire::market_data {

/// Immutable bundle of quotes fed into pricing (`IMarketData`).
///
/// Sprint 8 keeps this intentionally small: equity spots, a single discount
/// rate, per-name dividend yields, and one flat Black–Scholes-style vol used
/// for every \((K,T)\) slice. Finer structure (surfaces, curves) can extend
/// this type later without changing `IMarketData`.
struct MarketSnapshot {
    std::unordered_map<std::string, double> spots;
    double risk_free_rate{0.0};
    std::unordered_map<std::string, double> dividend_yields;
    /// Used for all `ImpliedVolatility` calls until a surface ships.
    double flat_implied_volatility{0.0};
};

}  // namespace numeraire::market_data
