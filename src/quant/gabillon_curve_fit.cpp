#include <numeraire/quant/gabillon_curve_fit.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

namespace numeraire::quant {
namespace {

constexpr double kPivotTol = 1.0e-14;
constexpr int kMeanReversionGridSteps = 600;
constexpr int kRefinementPasses = 4;

/// A factor pair sitting at exactly ±1 is one factor wearing two hats: the correlation
/// matrix built from it is singular and `CholeskyDecompose` rejects it outright. A steep
/// enough vol term structure does push the least-squares cross term to the boundary, so
/// stop just short of it — economically the same statement, numerically still usable.
constexpr double kMaxFactorCorrelation = 0.999;

/// Solve a small symmetric positive-definite system in place by Cholesky.
/// `quant::CholeskyDecompose` is deliberately restricted to correlation matrices, so
/// normal equations need their own factorization.
[[nodiscard]] bool SolveSpd(std::vector<double>& a, std::vector<double>& b, const std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j <= i; ++j) {
            double sum = a[(i * n) + j];
            for (std::size_t p = 0; p < j; ++p) {
                sum -= a[(i * n) + p] * a[(j * n) + p];
            }
            if (i == j) {
                if (sum <= kPivotTol) {
                    return false;
                }
                a[(i * n) + i] = std::sqrt(sum);
            } else {
                a[(i * n) + j] = sum / a[(j * n) + j];
            }
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        double sum = b[i];
        for (std::size_t p = 0; p < i; ++p) {
            sum -= a[(i * n) + p] * b[p];
        }
        b[i] = sum / a[(i * n) + i];
    }
    for (std::size_t i = n; i-- > 0;) {
        double sum = b[i];
        for (std::size_t p = i + 1; p < n; ++p) {
            sum -= a[(p * n) + i] * b[p];
        }
        b[i] = sum / a[(i * n) + i];
    }
    return true;
}

/// Design row: short-factor weight, long-factor weight, then the harmonic pairs.
void FillDesignRow(const ForwardCurvePoint& point, const double mean_reversion, const int num_harmonics,
                   std::vector<double>& row) {
    const double decay = std::exp(-mean_reversion * point.time_to_settlement_years);
    row[0] = decay;
    row[1] = 1.0 - decay;
    for (int h = 1; h <= num_harmonics; ++h) {
        const double angle = 2.0 * std::numbers::pi * static_cast<double>(h) * point.year_phase;
        const std::size_t base = 2U * static_cast<std::size_t>(h);
        row[base] = std::cos(angle);
        row[base + 1U] = std::sin(angle);
    }
}

[[nodiscard]] bool CurveUsable(const std::vector<ForwardCurvePoint>& curve, const int num_harmonics) {
    if (num_harmonics < 0) {
        return false;
    }
    const std::size_t num_unknowns = 2U + (2U * static_cast<std::size_t>(num_harmonics));
    if (curve.size() < num_unknowns) {
        return false;
    }
    return std::all_of(curve.begin(), curve.end(), [](const ForwardCurvePoint& p) {
        return p.price > 0.0 && std::isfinite(p.price) && std::isfinite(p.time_to_settlement_years) &&
               p.time_to_settlement_years >= 0.0 && std::isfinite(p.year_phase);
    });
}

}  // namespace

double SeasonalValue(const SeasonalShape& shape, const double year_phase) {
    double out = 0.0;
    for (std::size_t i = 0; i < shape.cos_coeffs.size(); ++i) {
        const double angle = 2.0 * std::numbers::pi * static_cast<double>(i + 1U) * year_phase;
        out += shape.cos_coeffs[i] * std::cos(angle);
        if (i < shape.sin_coeffs.size()) {
            out += shape.sin_coeffs[i] * std::sin(angle);
        }
    }
    return out;
}

double GabillonCurveLogPrice(const GabillonCurveFit& fit, const double time_to_settlement_years,
                             const double year_phase) {
    const double decay = std::exp(-fit.mean_reversion * time_to_settlement_years);
    return (decay * std::log(fit.short_factor_level)) + ((1.0 - decay) * std::log(fit.long_factor_level)) +
           SeasonalValue(fit.seasonal, year_phase);
}

GabillonCurveFit FitGabillonCurveGivenMeanReversion(const std::vector<ForwardCurvePoint>& curve,
                                                    const double mean_reversion, const int num_harmonics) {
    GabillonCurveFit fit;
    if (!CurveUsable(curve, num_harmonics) || !(mean_reversion > 0.0)) {
        fit.status = GabillonFitStatus::kInvalidInputs;
        return fit;
    }

    const std::size_t n = 2U + (2U * static_cast<std::size_t>(num_harmonics));
    std::vector<double> normal(n * n, 0.0);
    std::vector<double> rhs(n, 0.0);
    std::vector<double> row(n, 0.0);

    for (const ForwardCurvePoint& point : curve) {
        FillDesignRow(point, mean_reversion, num_harmonics, row);
        const double log_price = std::log(point.price);
        for (std::size_t i = 0; i < n; ++i) {
            rhs[i] += row[i] * log_price;
            for (std::size_t j = 0; j <= i; ++j) {
                normal[(i * n) + j] += row[i] * row[j];
            }
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            normal[(i * n) + j] = normal[(j * n) + i];
        }
    }

    if (!SolveSpd(normal, rhs, n)) {
        fit.status = GabillonFitStatus::kSingularSystem;
        return fit;
    }

    fit.status = GabillonFitStatus::kOk;
    fit.mean_reversion = mean_reversion;
    fit.short_factor_level = std::exp(rhs[0]);
    fit.long_factor_level = std::exp(rhs[1]);
    fit.seasonal.cos_coeffs.reserve(static_cast<std::size_t>(num_harmonics));
    fit.seasonal.sin_coeffs.reserve(static_cast<std::size_t>(num_harmonics));
    for (int h = 1; h <= num_harmonics; ++h) {
        const std::size_t base = 2U * static_cast<std::size_t>(h);
        fit.seasonal.cos_coeffs.push_back(rhs[base]);
        fit.seasonal.sin_coeffs.push_back(rhs[base + 1U]);
    }
    fit.num_points = curve.size();

    double sse = 0.0;
    for (const ForwardCurvePoint& point : curve) {
        const double residual =
                std::log(point.price) - GabillonCurveLogPrice(fit, point.time_to_settlement_years, point.year_phase);
        sse += residual * residual;
    }
    fit.log_rmse = std::sqrt(sse / static_cast<double>(curve.size()));
    return fit;
}

GabillonCurveFit FitGabillonCurve(const std::vector<ForwardCurvePoint>& curve, const int num_harmonics,
                                  const double min_mean_reversion, const double max_mean_reversion) {
    GabillonCurveFit best;
    if (!CurveUsable(curve, num_harmonics) || !(min_mean_reversion > 0.0) ||
        !(max_mean_reversion > min_mean_reversion)) {
        best.status = GabillonFitStatus::kInvalidInputs;
        return best;
    }

    // Mean reversion enters non-linearly, everything else does not. Scanning it on a log
    // grid and refining around the winner is both cheaper and steadier than a joint
    // non-linear solve, because each trial is an exactly solved linear problem.
    double lo = std::log(min_mean_reversion);
    double hi = std::log(max_mean_reversion);
    for (int pass = 0; pass < kRefinementPasses; ++pass) {
        const double step = (hi - lo) / static_cast<double>(kMeanReversionGridSteps);
        double best_log_k = lo;
        for (int i = 0; i <= kMeanReversionGridSteps; ++i) {
            const double log_k = lo + (step * static_cast<double>(i));
            const GabillonCurveFit trial =
                    FitGabillonCurveGivenMeanReversion(curve, std::exp(log_k), num_harmonics);
            if (trial.status != GabillonFitStatus::kOk) {
                continue;
            }
            if (best.status != GabillonFitStatus::kOk || trial.log_rmse < best.log_rmse) {
                best = trial;
                best_log_k = log_k;
            }
        }
        if (best.status != GabillonFitStatus::kOk) {
            return best;
        }
        lo = std::max(lo, best_log_k - step);
        hi = std::min(hi, best_log_k + step);
        if (!(hi > lo)) {
            break;
        }
    }
    return best;
}

double GabillonForwardVolatility(const double time_to_settlement_years, const double mean_reversion,
                                 const double short_factor_vol, const double long_factor_vol,
                                 const double factor_correlation) noexcept {
    const double decay = std::exp(-mean_reversion * time_to_settlement_years);
    const double carry = 1.0 - decay;
    const double variance = (short_factor_vol * short_factor_vol * decay * decay) +
                            (long_factor_vol * long_factor_vol * carry * carry) +
                            (2.0 * factor_correlation * short_factor_vol * long_factor_vol * decay * carry);
    return variance > 0.0 ? std::sqrt(variance) : 0.0;
}

GabillonVolFit FitGabillonVolatilities(const std::vector<double>& time_to_settlement_years,
                                       const std::vector<double>& volatilities,
                                       const double min_mean_reversion, const double max_mean_reversion) {
    GabillonVolFit fit;
    if (time_to_settlement_years.size() != volatilities.size() || volatilities.size() < 3U ||
        !(min_mean_reversion > 0.0) || !(max_mean_reversion > min_mean_reversion)) {
        fit.status = GabillonFitStatus::kInvalidInputs;
        return fit;
    }
    for (std::size_t i = 0; i < volatilities.size(); ++i) {
        if (!(volatilities[i] > 0.0) || !(time_to_settlement_years[i] >= 0.0)) {
            fit.status = GabillonFitStatus::kInvalidInputs;
            return fit;
        }
    }

    // Squared vol is linear in the three products (sigma_S^2, sigma_L^2, rho sigma_S sigma_L)
    // once k is fixed, so the same scan-then-solve trick works here.
    double lo = std::log(min_mean_reversion);
    double hi = std::log(max_mean_reversion);
    for (int pass = 0; pass < kRefinementPasses; ++pass) {
        const double step = (hi - lo) / static_cast<double>(kMeanReversionGridSteps);
        double best_log_k = lo;
        for (int i = 0; i <= kMeanReversionGridSteps; ++i) {
            const double log_k = lo + (step * static_cast<double>(i));
            const double k = std::exp(log_k);

            constexpr std::size_t kDim = 3;
            std::vector<double> normal(kDim * kDim, 0.0);
            std::vector<double> rhs(kDim, 0.0);
            for (std::size_t p = 0; p < volatilities.size(); ++p) {
                const double decay = std::exp(-k * time_to_settlement_years[p]);
                const double carry = 1.0 - decay;
                const std::array<double, kDim> row{decay * decay, carry * carry, 2.0 * decay * carry};
                const double target = volatilities[p] * volatilities[p];
                for (std::size_t a = 0; a < kDim; ++a) {
                    rhs[a] += row.at(a) * target;
                    for (std::size_t b = 0; b < kDim; ++b) {
                        normal[(a * kDim) + b] += row.at(a) * row.at(b);
                    }
                }
            }
            std::vector<double> solution = rhs;
            std::vector<double> matrix = normal;
            if (!SolveSpd(matrix, solution, kDim)) {
                continue;
            }
            if (!(solution[0] > 0.0) || !(solution[1] > 0.0)) {
                continue;
            }
            const double sigma_s = std::sqrt(solution[0]);
            const double sigma_l = std::sqrt(solution[1]);
            const double rho = std::clamp(solution[2] / (sigma_s * sigma_l), -kMaxFactorCorrelation,
                                          kMaxFactorCorrelation);

            double sse = 0.0;
            for (std::size_t p = 0; p < volatilities.size(); ++p) {
                const double model =
                        GabillonForwardVolatility(time_to_settlement_years[p], k, sigma_s, sigma_l, rho);
                const double residual = model - volatilities[p];
                sse += residual * residual;
            }
            const double rmse = std::sqrt(sse / static_cast<double>(volatilities.size()));
            if (fit.status != GabillonFitStatus::kOk || rmse < fit.rmse) {
                fit.status = GabillonFitStatus::kOk;
                fit.mean_reversion = k;
                fit.short_factor_vol = sigma_s;
                fit.long_factor_vol = sigma_l;
                fit.factor_correlation = rho;
                fit.rmse = rmse;
                best_log_k = log_k;
            }
        }
        if (fit.status != GabillonFitStatus::kOk) {
            return fit;
        }
        lo = std::max(lo, best_log_k - step);
        hi = std::min(hi, best_log_k + step);
        if (!(hi > lo)) {
            break;
        }
    }
    return fit;
}

}  // namespace numeraire::quant
