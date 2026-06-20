#pragma once

namespace numeraire::simulation {

/// Flat market quotes for path-wise pricing (v1 — same conventions as dev_main MTM env).
struct PathPricingQuotes {
    double risk_free_rate{0.03};
    double dividend_yield{0.0};
    double flat_implied_volatility{0.20};
};

}  // namespace numeraire::simulation
