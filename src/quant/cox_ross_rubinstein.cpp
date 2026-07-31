#include <numeraire/quant/cox_ross_rubinstein.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace numeraire::quant {
namespace {

[[nodiscard]] double Payoff(const OptionType kind, const double spot, const double strike) {
    if (kind == OptionType::kCall) {
        return std::max(spot - strike, 0.0);
    }
    return std::max(strike - spot, 0.0);
}

}  // namespace

double CoxRossRubinsteinVanillaPrice(const OptionType option_type,
                                     const ExerciseStyle exercise,
                                     const double spot,
                                     const double strike,
                                     const double risk_free_rate,
                                     const double dividend_yield,
                                     const double volatility,
                                     const double time_to_expiry_years,
                                     const std::size_t n_steps) {
    if (spot <= 0.0 || strike <= 0.0) {
        return 0.0;
    }
    if (time_to_expiry_years <= 0.0) {
        return Payoff(option_type, spot, strike);
    }
    if (n_steps == 0) {
        return Payoff(option_type, spot, strike);
    }
    if (volatility <= 0.0) {
        // Deterministic forward intrinsic (no tree).
        const double forward = spot * std::exp((risk_free_rate - dividend_yield) * time_to_expiry_years);
        const double df = std::exp(-risk_free_rate * time_to_expiry_years);
        if (exercise == ExerciseStyle::kAmerican) {
            // Rough bound: at least intrinsic; for zero vol European DF*intrinsic(F).
            const double european = df * Payoff(option_type, forward, strike);
            return std::max(Payoff(option_type, spot, strike), european);
        }
        return df * Payoff(option_type, forward, strike);
    }

    const int n = static_cast<int>(n_steps);
    const double dt = time_to_expiry_years / static_cast<double>(n);
    const double u = std::exp(volatility * std::sqrt(dt));
    const double d = 1.0 / u;
    const double growth = std::exp((risk_free_rate - dividend_yield) * dt);
    double p = (growth - d) / (u - d);
    // Numerical guard if parameters push p outside (0,1).
    p = std::min(1.0, std::max(0.0, p));
    const double q = 1.0 - p;
    const double disc = std::exp(-risk_free_rate * dt);
    const bool american = exercise == ExerciseStyle::kAmerican;

    // Flat working buffer size n+1: at step k, only indices 0..k are live.
    // v[i] = option value after `i` up-moves (and k-i downs).
    std::vector<double> v(static_cast<std::size_t>(n) + 1U);
    const double ud = u / d;

    {
        double s = spot * std::pow(d, n);
        for (int i = 0; i <= n; ++i) {
            v[static_cast<std::size_t>(i)] = Payoff(option_type, s, strike);
            s *= ud;
        }
    }

    for (int step = n - 1; step >= 0; --step) {
        if (american) {
            double s = spot * std::pow(d, step);
            for (int i = 0; i <= step; ++i) {
                const double cont =
                        disc * (p * v[static_cast<std::size_t>(i + 1)] + q * v[static_cast<std::size_t>(i)]);
                v[static_cast<std::size_t>(i)] = std::max(Payoff(option_type, s, strike), cont);
                s *= ud;
            }
        } else {
            for (int i = 0; i <= step; ++i) {
                v[static_cast<std::size_t>(i)] =
                        disc * (p * v[static_cast<std::size_t>(i + 1)] + q * v[static_cast<std::size_t>(i)]);
            }
        }
    }

    return v[0];
}

}  // namespace numeraire::quant
