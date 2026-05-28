#include <numeraire/market_data/vol_surface_interpolation.hpp>

#include <numeraire/utils/exception.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <vector>

namespace numeraire::market_data {
namespace {

constexpr double kTauEpsilon = 1.0e-8;

[[nodiscard]] double LinearClamp(const double x, const double x0, const double y0, const double x1, const double y1) {
    if (std::abs(x1 - x0) < 1.0e-14) {
        return 0.5 * (y0 + y1);
    }
    const double t = (x - x0) / (x1 - x0);
    return y0 + t * (y1 - y0);
}

[[nodiscard]] double InterpolateAlongLogMoneyness(const std::vector<database::VolSurfaceGridPoint>& slice,
                                                const double log_moneyness) {
    if (slice.empty()) {
        throw MarketDataError("vol surface interpolation: empty slice");
    }
    if (slice.size() == 1U) {
        return slice.front().implied_vol;
    }

    std::vector<const database::VolSurfaceGridPoint*> sorted;
    sorted.reserve(slice.size());
    for (const auto& pt : slice) {
        sorted.push_back(&pt);
    }
    std::sort(sorted.begin(), sorted.end(), [](const database::VolSurfaceGridPoint* a,
                                                 const database::VolSurfaceGridPoint* b) {
        return a->log_moneyness < b->log_moneyness;
    });

    if (log_moneyness <= sorted.front()->log_moneyness) {
        return sorted.front()->implied_vol;
    }
    if (log_moneyness >= sorted.back()->log_moneyness) {
        return sorted.back()->implied_vol;
    }

    for (size_t i = 1; i < sorted.size(); ++i) {
        const auto* left = sorted[i - 1];
        const auto* right = sorted[i];
        if (log_moneyness <= right->log_moneyness) {
            return LinearClamp(log_moneyness, left->log_moneyness, left->implied_vol, right->log_moneyness,
                               right->implied_vol);
        }
    }

    return sorted.back()->implied_vol;
}

[[nodiscard]] std::map<double, std::vector<database::VolSurfaceGridPoint>> GroupByTau(
        const std::vector<database::VolSurfaceGridPoint>& points) {
    std::map<double, std::vector<database::VolSurfaceGridPoint>> by_tau;
    for (const auto& pt : points) {
        bool placed = false;
        for (auto& [tau, bucket] : by_tau) {
            if (std::abs(pt.years_to_maturity - tau) <= kTauEpsilon) {
                bucket.push_back(pt);
                placed = true;
                break;
            }
        }
        if (!placed) {
            by_tau.emplace(pt.years_to_maturity, std::vector<database::VolSurfaceGridPoint>{pt});
        }
    }
    return by_tau;
}

}  // namespace

double InterpolateImpliedVol(const std::vector<database::VolSurfaceGridPoint>& points,
                             const double log_moneyness,
                             const double years_to_maturity) {
    if (points.empty()) {
        throw MarketDataError("vol surface interpolation: no points");
    }
    if (years_to_maturity <= 0.0) {
        throw MarketDataError("vol surface interpolation: non-positive time to maturity");
    }

    const auto by_tau = GroupByTau(points);
    if (by_tau.empty()) {
        throw MarketDataError("vol surface interpolation: no tenor buckets");
    }

    if (by_tau.size() == 1U) {
        return InterpolateAlongLogMoneyness(by_tau.begin()->second, log_moneyness);
    }

    auto upper = by_tau.lower_bound(years_to_maturity);
    if (upper == by_tau.end()) {
        return InterpolateAlongLogMoneyness(by_tau.rbegin()->second, log_moneyness);
    }
    if (upper == by_tau.begin()) {
        return InterpolateAlongLogMoneyness(upper->second, log_moneyness);
    }

    auto lower = upper;
    --lower;
    const double iv_low = InterpolateAlongLogMoneyness(lower->second, log_moneyness);
    const double iv_high = InterpolateAlongLogMoneyness(upper->second, log_moneyness);

    return LinearClamp(years_to_maturity, lower->first, iv_low, upper->first, iv_high);
}

}  // namespace numeraire::market_data
