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

[[nodiscard]] double SpotAt(const double spot0,
                            const double u,
                            const double d,
                            const int step,
                            const int up_moves) {
    // S = S0 * u^{up} * d^{step-up}
    return spot0 * std::pow(u, up_moves) * std::pow(d, step - up_moves);
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

CrrTreeDump CoxRossRubinsteinVanillaTree(const OptionType option_type,
                                         const ExerciseStyle exercise,
                                         const double spot,
                                         const double strike,
                                         const double risk_free_rate,
                                         const double dividend_yield,
                                         const double volatility,
                                         const double time_to_expiry_years,
                                         const std::size_t n_steps) {
    CrrTreeDump out;
    out.n_steps = n_steps;

    if (spot <= 0.0 || strike <= 0.0 || n_steps == 0) {
        out.npv = (spot > 0.0 && strike > 0.0) ? Payoff(option_type, spot, strike) : 0.0;
        if (spot > 0.0 && strike > 0.0) {
            const double intr = Payoff(option_type, spot, strike);
            out.nodes.push_back(CrrTreeNode{.step = 0,
                                            .up_moves = 0,
                                            .spot = spot,
                                            .intrinsic = intr,
                                            .continuation = intr,
                                            .value = out.npv,
                                            .early_exercise = false});
        }
        return out;
    }

    if (time_to_expiry_years <= 0.0 || volatility <= 0.0) {
        out.npv = CoxRossRubinsteinVanillaPrice(option_type, exercise, spot, strike, risk_free_rate, dividend_yield,
                                                volatility, time_to_expiry_years, n_steps);
        const double intr = Payoff(option_type, spot, strike);
        out.nodes.push_back(CrrTreeNode{.step = 0,
                                        .up_moves = 0,
                                        .spot = spot,
                                        .intrinsic = intr,
                                        .continuation = out.npv,
                                        .value = out.npv,
                                        .early_exercise = false});
        return out;
    }

    const int n = static_cast<int>(n_steps);
    const double dt = time_to_expiry_years / static_cast<double>(n);
    const double u = std::exp(volatility * std::sqrt(dt));
    const double d = 1.0 / u;
    const double growth = std::exp((risk_free_rate - dividend_yield) * dt);
    double p = (growth - d) / (u - d);
    p = std::min(1.0, std::max(0.0, p));
    const double q = 1.0 - p;
    const double disc = std::exp(-risk_free_rate * dt);
    const bool american = exercise == ExerciseStyle::kAmerican;

    out.dt = dt;
    out.u = u;
    out.d = d;
    out.p_up = p;
    out.discount = disc;

    // values[step][i]
    std::vector<std::vector<double>> values(static_cast<std::size_t>(n) + 1U);
    std::vector<std::vector<double>> conts(static_cast<std::size_t>(n) + 1U);
    std::vector<std::vector<double>> intrinsics(static_cast<std::size_t>(n) + 1U);

    values[static_cast<std::size_t>(n)].resize(static_cast<std::size_t>(n) + 1U);
    conts[static_cast<std::size_t>(n)].resize(static_cast<std::size_t>(n) + 1U);
    intrinsics[static_cast<std::size_t>(n)].resize(static_cast<std::size_t>(n) + 1U);
    for (int i = 0; i <= n; ++i) {
        const double s = SpotAt(spot, u, d, n, i);
        const double intr = Payoff(option_type, s, strike);
        values[static_cast<std::size_t>(n)][static_cast<std::size_t>(i)] = intr;
        conts[static_cast<std::size_t>(n)][static_cast<std::size_t>(i)] = intr;
        intrinsics[static_cast<std::size_t>(n)][static_cast<std::size_t>(i)] = intr;
    }

    for (int step = n - 1; step >= 0; --step) {
        values[static_cast<std::size_t>(step)].resize(static_cast<std::size_t>(step) + 1U);
        conts[static_cast<std::size_t>(step)].resize(static_cast<std::size_t>(step) + 1U);
        intrinsics[static_cast<std::size_t>(step)].resize(static_cast<std::size_t>(step) + 1U);
        for (int i = 0; i <= step; ++i) {
            const double s = SpotAt(spot, u, d, step, i);
            const double intr = Payoff(option_type, s, strike);
            const double cont = disc * (p * values[static_cast<std::size_t>(step + 1)][static_cast<std::size_t>(i + 1)] +
                                        q * values[static_cast<std::size_t>(step + 1)][static_cast<std::size_t>(i)]);
            const double val = american ? std::max(intr, cont) : cont;
            values[static_cast<std::size_t>(step)][static_cast<std::size_t>(i)] = val;
            conts[static_cast<std::size_t>(step)][static_cast<std::size_t>(i)] = cont;
            intrinsics[static_cast<std::size_t>(step)][static_cast<std::size_t>(i)] = intr;
        }
    }

    out.npv = values[0][0];
    out.nodes.reserve(static_cast<std::size_t>((n + 1) * (n + 2) / 2));
    constexpr double kEps = 1e-12;
    for (int step = 0; step <= n; ++step) {
        for (int i = 0; i <= step; ++i) {
            const double intr = intrinsics[static_cast<std::size_t>(step)][static_cast<std::size_t>(i)];
            const double cont = conts[static_cast<std::size_t>(step)][static_cast<std::size_t>(i)];
            const double val = values[static_cast<std::size_t>(step)][static_cast<std::size_t>(i)];
            const bool early = american && step < n && (intr > cont + kEps);
            out.nodes.push_back(CrrTreeNode{.step = step,
                                            .up_moves = i,
                                            .spot = SpotAt(spot, u, d, step, i),
                                            .intrinsic = intr,
                                            .continuation = cont,
                                            .value = val,
                                            .early_exercise = early});
        }
    }
    return out;
}

}  // namespace numeraire::quant
