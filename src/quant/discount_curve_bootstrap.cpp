#include <numeraire/quant/discount_curve_bootstrap.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace numeraire::quant {
namespace {

constexpr double kTimeTol = 1.0e-10;
constexpr double kZeroTol = 1.0e-10;
constexpr double kParPricingTol = 1.0e-10;
constexpr int kMaxBisectIter = 120;
constexpr double kMaxZeroRate = 1.0;

struct SolvedNode {
    double time_years{0.0};
    double zero_rate{0.0};
};

[[nodiscard]] bool QuoteValid(const CurvePillarQuote& quote) {
    return quote.tenor_days > 0 && quote.quoted_rate >= 0.0 && std::isfinite(quote.quoted_rate);
}

[[nodiscard]] bool QuotesSortedByTenor(const std::vector<CurvePillarQuote>& quotes) {
    for (std::size_t i = 1; i < quotes.size(); ++i) {
        if (quotes[i].tenor_days <= quotes[i - 1].tenor_days) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] double InterpolateZeroRateLinear(const std::vector<SolvedNode>& nodes,
                                                const double time_years,
                                                const double terminal_time_years,
                                                const double terminal_zero_rate) {
    std::vector<SolvedNode> curve = nodes;
    curve.push_back({terminal_time_years, terminal_zero_rate});
    std::sort(curve.begin(), curve.end(), [](const SolvedNode& a, const SolvedNode& b) {
        return a.time_years < b.time_years;
    });

    if (time_years <= curve.front().time_years + kTimeTol) {
        return curve.front().zero_rate;
    }
    if (time_years + kTimeTol >= curve.back().time_years) {
        return curve.back().zero_rate;
    }

    for (std::size_t i = 0; i + 1 < curve.size(); ++i) {
        const double t0 = curve[i].time_years;
        const double t1 = curve[i + 1].time_years;
        if (time_years + kTimeTol < t0 || time_years > t1 + kTimeTol) {
            continue;
        }
        if (std::abs(t1 - t0) <= kTimeTol) {
            return curve[i].zero_rate;
        }
        const double weight = (time_years - t0) / (t1 - t0);
        return curve[i].zero_rate + (weight * (curve[i + 1].zero_rate - curve[i].zero_rate));
    }

    return curve.back().zero_rate;
}

[[nodiscard]] double DiscountFactorAtTime(const std::vector<SolvedNode>& nodes,
                                           const double time_years,
                                           const double terminal_time_years,
                                           const double terminal_zero_rate) {
    const double zero_rate =
            InterpolateZeroRateLinear(nodes, time_years, terminal_time_years, terminal_zero_rate);
    return DiscountFactorFromZeroRate(zero_rate, time_years);
}

[[nodiscard]] std::vector<double> SemiAnnualCouponTimes(const double maturity_years) {
    std::vector<double> times;
    const int periods = static_cast<int>(std::lround(maturity_years / 0.5));
    if (periods <= 0) {
        return times;
    }
    times.reserve(static_cast<std::size_t>(periods));
    for (int k = 1; k <= periods; ++k) {
        times.push_back(0.5 * static_cast<double>(k));
    }
    return times;
}

[[nodiscard]] double SwapParPricingError(const std::vector<SolvedNode>& nodes,
                                          const double maturity_years,
                                          const double quoted_rate,
                                          const double candidate_zero_rate) {
    const double coupon = 0.5 * quoted_rate;
    const std::vector<double> coupon_times = SemiAnnualCouponTimes(maturity_years);
    if (coupon_times.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    double pv = 0.0;
    for (std::size_t j = 0; j + 1 < coupon_times.size(); ++j) {
        pv += coupon * DiscountFactorAtTime(nodes, coupon_times[j], maturity_years, candidate_zero_rate);
    }

    const double terminal_df = DiscountFactorFromZeroRate(candidate_zero_rate, maturity_years);
    pv += (1.0 + coupon) * terminal_df;
    return pv - 1.0;
}

[[nodiscard]] BootstrapStatus SolveSwapZeroRate(const std::vector<SolvedNode>& nodes,
                                                 const double maturity_years,
                                                 const double quoted_rate,
                                                 double& out_zero_rate) {
    const double f0 = SwapParPricingError(nodes, maturity_years, quoted_rate, 0.0);
    const double f_hi = SwapParPricingError(nodes, maturity_years, quoted_rate, kMaxZeroRate);
    if (!std::isfinite(f0) || !std::isfinite(f_hi)) {
        return BootstrapStatus::kInvalidInputs;
    }
    if (f0 <= 0.0) {
        out_zero_rate = 0.0;
        return BootstrapStatus::kOk;
    }
    if (f_hi >= 0.0) {
        return BootstrapStatus::kNoConvergence;
    }

    double lo = 0.0;
    double hi = kMaxZeroRate;
    for (int i = 0; i < kMaxBisectIter; ++i) {
        const double mid = 0.5 * (lo + hi);
        const double f_mid = SwapParPricingError(nodes, maturity_years, quoted_rate, mid);
        if (!std::isfinite(f_mid)) {
            return BootstrapStatus::kNoConvergence;
        }
        if (std::abs(f_mid) < kParPricingTol) {
            out_zero_rate = mid;
            return BootstrapStatus::kOk;
        }
        if (f_mid > 0.0) {
            lo = mid;
        } else {
            hi = mid;
        }
    }

    out_zero_rate = 0.5 * (lo + hi);
    const double f_final = SwapParPricingError(nodes, maturity_years, quoted_rate, out_zero_rate);
    if (std::abs(f_final) < 1.0e-8) {
        return BootstrapStatus::kOk;
    }
    return BootstrapStatus::kNoConvergence;
}

[[nodiscard]] BootstrappedCurvePoint MakePoint(const CurvePillarQuote& quote,
                                                const double zero_rate) {
    const double time_years = CurvePillarTimeYears(quote.tenor_days);
    return BootstrappedCurvePoint{
            quote.tenor,
            time_years,
            zero_rate,
            DiscountFactorFromZeroRate(zero_rate, time_years),
    };
}

}  // namespace

double DepositZeroRateFromQuote(const double quoted_rate, const double time_years) noexcept {
    if (time_years <= 0.0) {
        return 0.0;
    }
    const double discount_factor = 1.0 / (1.0 + (quoted_rate * time_years));
    if (discount_factor <= 0.0) {
        return 0.0;
    }
    return -std::log(discount_factor) / time_years;
}

double DiscountFactorFromZeroRate(const double zero_rate, const double time_years) noexcept {
    if (time_years <= 0.0) {
        return 1.0;
    }
    return std::exp(-zero_rate * time_years);
}

BootstrapResult BootstrapDiscountCurve(const std::vector<CurvePillarQuote>& quotes) {
    BootstrapResult result;
    if (quotes.empty()) {
        return result;
    }
    for (const CurvePillarQuote& quote : quotes) {
        if (!QuoteValid(quote)) {
            return result;
        }
    }
    if (!QuotesSortedByTenor(quotes)) {
        return result;
    }

    std::vector<SolvedNode> nodes;
    nodes.reserve(quotes.size());
    result.pillars.reserve(quotes.size());

    for (const CurvePillarQuote& quote : quotes) {
        const double time_years = CurvePillarTimeYears(quote.tenor_days);
        if (time_years <= kZeroTol) {
            return result;
        }

        double zero_rate = 0.0;
        switch (quote.kind) {
            case CurvePillarKind::kDeposit: {
                zero_rate = DepositZeroRateFromQuote(quote.quoted_rate, time_years);
                break;
            }
            case CurvePillarKind::kSwap: {
                const BootstrapStatus swap_status =
                        SolveSwapZeroRate(nodes, time_years, quote.quoted_rate, zero_rate);
                if (swap_status != BootstrapStatus::kOk) {
                    result.status = swap_status;
                    return result;
                }
                break;
            }
            default:
                result.status = BootstrapStatus::kUnsupportedInstrument;
                return result;
        }

        if (!std::isfinite(zero_rate) || zero_rate < 0.0) {
            result.status = BootstrapStatus::kNoConvergence;
            return result;
        }

        nodes.push_back({time_years, zero_rate});
        result.pillars.push_back(MakePoint(quote, zero_rate));
    }

    result.status = BootstrapStatus::kOk;
    return result;
}

}  // namespace numeraire::quant
