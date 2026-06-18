#include <numeraire/quant/nearest_correlation.hpp>

#include <cmath>
#include <span>
#include <vector>

namespace numeraire::quant {
namespace {

constexpr double kSymmetryTol = 1.0e-10;
constexpr double kJacobiTol = 1.0e-12;
constexpr int kJacobiMaxIter = 200;

[[nodiscard]] double At(const std::span<const double> matrix, const std::size_t n, const std::size_t row,
                        const std::size_t col) {
    return matrix[(row * n) + col];
}

[[nodiscard]] double& AtMut(std::vector<double>& matrix, const std::size_t n, const std::size_t row,
                              const std::size_t col) {
    return matrix[(row * n) + col];
}

[[nodiscard]] bool SymmetricFinite(const std::span<const double> matrix, const std::size_t n) {
    if (n == 0 || matrix.size() != n * n) {
        return false;
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (!std::isfinite(At(matrix, n, i, i))) {
            return false;
        }
        for (std::size_t j = 0; j < i; ++j) {
            const double a_ij = At(matrix, n, i, j);
            const double a_ji = At(matrix, n, j, i);
            if (!std::isfinite(a_ij) || !std::isfinite(a_ji)) {
                return false;
            }
            if (std::abs(a_ij - a_ji) > kSymmetryTol) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] double FrobeniusDiff(const std::span<const double> a, const std::span<const double> b,
                                   const std::size_t n) {
    double sum_sq = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            const double d = At(a, n, i, j) - At(b, n, i, j);
            sum_sq += d * d;
        }
    }
    return std::sqrt(sum_sq);
}

void ReplaceDiagonalWithOnes(std::vector<double>& matrix, const std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        AtMut(matrix, n, i, i) = 1.0;
    }
}

/// Symmetric Jacobi eigen-decomposition: `A = V * diag(eval) * V^T` (`V` row-major).
void SymmetricEigenJacobi(const std::span<const double> a, const std::size_t n, std::vector<double>& eval,
                          std::vector<double>& v) {
    std::vector<double> m(a.begin(), a.end());
    v.assign(n * n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        AtMut(v, n, i, i) = 1.0;
    }
    eval.assign(n, 0.0);

    for (int sweep = 0; sweep < kJacobiMaxIter; ++sweep) {
        double max_off = 0.0;
        std::size_t p = 0;
        std::size_t q = 1;
        for (std::size_t i = 0; i + 1 < n; ++i) {
            for (std::size_t j = i + 1; j < n; ++j) {
                const double off = std::abs(At(m, n, i, j));
                if (off > max_off) {
                    max_off = off;
                    p = i;
                    q = j;
                }
            }
        }
        if (max_off <= kJacobiTol) {
            break;
        }

        const double app = At(m, n, p, p);
        const double aqq = At(m, n, q, q);
        const double apq = At(m, n, p, q);
        const double tau = (aqq - app) / (2.0 * apq);
        const double t = (tau >= 0.0 ? 1.0 : -1.0) / (std::abs(tau) + std::sqrt(1.0 + (tau * tau)));
        const double c = 1.0 / std::sqrt(1.0 + (t * t));
        const double s = t * c;

        AtMut(m, n, p, p) = app - (t * apq);
        AtMut(m, n, q, q) = aqq + (t * apq);
        AtMut(m, n, p, q) = 0.0;
        AtMut(m, n, q, p) = 0.0;

        for (std::size_t k = 0; k < n; ++k) {
            if (k == p || k == q) {
                continue;
            }
            const double mkp = At(m, n, k, p);
            const double mkq = At(m, n, k, q);
            AtMut(m, n, k, p) = (c * mkp) - (s * mkq);
            AtMut(m, n, p, k) = At(m, n, k, p);
            AtMut(m, n, k, q) = (s * mkp) + (c * mkq);
            AtMut(m, n, q, k) = At(m, n, k, q);
        }

        for (std::size_t k = 0; k < n; ++k) {
            const double vkp = At(v, n, k, p);
            const double vkq = At(v, n, k, q);
            AtMut(v, n, k, p) = (c * vkp) - (s * vkq);
            AtMut(v, n, k, q) = (s * vkp) + (c * vkq);
        }
    }

    for (std::size_t i = 0; i < n; ++i) {
        eval[i] = At(m, n, i, i);
    }
}

void ProjectPsdSpectral(const std::span<const double> input, const std::size_t n,
                        std::vector<double>& output) {
    std::vector<double> eval;
    std::vector<double> v;
    SymmetricEigenJacobi(input, n, eval, v);

    output.assign(n * n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        const double lambda = std::max(eval[i], 0.0);
        if (lambda <= 0.0) {
            continue;
        }
        for (std::size_t r = 0; r < n; ++r) {
            for (std::size_t c = 0; c < n; ++c) {
                AtMut(output, n, r, c) += lambda * At(v, n, r, i) * At(v, n, c, i);
            }
        }
    }
}

[[nodiscard]] bool IsCorrelationMatrix(const std::span<const double> matrix, const std::size_t n,
                                       const double diag_tol) {
    if (!SymmetricFinite(matrix, n)) {
        return false;
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (std::abs(At(matrix, n, i, i) - 1.0) > diag_tol) {
            return false;
        }
    }
    std::vector<double> eval;
    std::vector<double> v;
    SymmetricEigenJacobi(matrix, n, eval, v);
    for (const double lambda : eval) {
        if (lambda < -1.0e-8) {
            return false;
        }
    }
    return true;
}

}  // namespace

NearestCorrelationResult NearestCorrelationHigham(const std::span<const double> matrix_row_major,
                                                  const std::size_t n, const int max_iterations,
                                                  const double tolerance) {
    NearestCorrelationResult result;
    if (!SymmetricFinite(matrix_row_major, n) || max_iterations <= 0 || tolerance <= 0.0) {
        return result;
    }

    std::vector<double> y(matrix_row_major.begin(), matrix_row_major.end());
    std::vector<double> r(n * n, 0.0);
    std::vector<double> x(n * n, 0.0);

    double delta = tolerance + 1.0;
    int iter = 0;
    for (; iter < max_iterations; ++iter) {
        r = y;
        ReplaceDiagonalWithOnes(r, n);
        ProjectPsdSpectral(r, n, x);
        delta = FrobeniusDiff(x, y, n);
        y = x;
        if (delta <= tolerance) {
            break;
        }
    }

    ReplaceDiagonalWithOnes(y, n);
    result.n = n;
    result.matrix = std::move(y);
    result.iterations = iter + 1;
    result.final_delta = delta;
    result.status = (delta <= tolerance) ? NearestCorrelationStatus::kOk : NearestCorrelationStatus::kNoConvergence;

    if (result.status == NearestCorrelationStatus::kOk &&
        !IsCorrelationMatrix(result.matrix, n, 1.0e-6)) {
        result.status = NearestCorrelationStatus::kNoConvergence;
    }
    return result;
}

}  // namespace numeraire::quant
