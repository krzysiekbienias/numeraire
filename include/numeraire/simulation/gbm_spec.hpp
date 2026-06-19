#pragma once

namespace numeraire::simulation {

/// Constant-coefficient single-underlying GBM inputs for one simulation batch.
struct SingleFactorGbmSpec {
    double spot{0.0};
    double risk_free_rate{0.0};
    double dividend_yield{0.0};
    double volatility{0.0};
};

}  // namespace numeraire::simulation
