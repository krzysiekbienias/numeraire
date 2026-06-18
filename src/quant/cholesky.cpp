#include <numeraire/quant/cholesky.hpp>

#include <cmath>
#include <span>
#include <vector>

namespace numeraire::quant {
namespace {

constexpr double kDiagOneTol = 1.0e-8;
constexpr double kSymmetryTol = 1.0e-10;
constexpr double kPivotTol = 1.0e-12;

[[nodiscard]] double LowerAt(const CholeskyFactor& factor, const std::size_t row,
                             const std::size_t col) {
    return factor.lower[(row * factor.n) + col];
}

[[nodiscard]] bool MatrixValid(const std::span<const double> matrix, const std::size_t n) {
    if (n == 0 || matrix.size() != n * n) {
        return false;
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (!std::isfinite(matrix[(i * n) + i])) {
            return false;
        }
        if (std::abs(matrix[(i * n) + i] - 1.0) > kDiagOneTol) {
            return false;
        }
        for (std::size_t j = 0; j < i; ++j) {
            const double a_ij = matrix[(i * n) + j];
            const double a_ji = matrix[(j * n) + i];
            if (!std::isfinite(a_ij) || !std::isfinite(a_ji)) {
                return false;
            }
            if (std::abs(a_ij - a_ji) > kSymmetryTol) {
                return false;
            }
            if (a_ij < -1.0 - kSymmetryTol || a_ij > 1.0 + kSymmetryTol) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

CholeskyResult CholeskyDecompose(const std::span<const double> matrix_row_major, const std::size_t n) {
    CholeskyResult result;
    if (!MatrixValid(matrix_row_major, n)) {
        return result;
    }

    CholeskyFactor factor;
    factor.n = n;
    factor.lower.assign(n * n, 0.0);

    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j <= i; ++j) {
            double sum = matrix_row_major[(i * n) + j];
            for (std::size_t k = 0; k < j; ++k) {
                sum -= LowerAt(factor, i, k) * LowerAt(factor, j, k);
            }
            if (i == j) {
                if (sum <= kPivotTol) {
                    result.status = CholeskyStatus::kNotPositiveDefinite;
                    return result;
                }
                factor.lower[(i * n) + j] = std::sqrt(sum);
            } else {
                const double pivot = LowerAt(factor, j, j);
                if (std::abs(pivot) <= kPivotTol) {
                    result.status = CholeskyStatus::kNotPositiveDefinite;
                    return result;
                }
                factor.lower[(i * n) + j] = sum / pivot;
            }
        }
    }

    result.status = CholeskyStatus::kOk;
    result.factor = std::move(factor);
    return result;
}

void ApplyLowerTriangular(const CholeskyFactor& factor, const std::span<const double> in,
                          const std::span<double> out) {
    if (factor.n == 0 || in.size() != factor.n || out.size() != factor.n) {
        return;
    }
    for (std::size_t i = 0; i < factor.n; ++i) {
        double sum = 0.0;
        for (std::size_t j = 0; j <= i; ++j) {
            sum += LowerAt(factor, i, j) * in[j];
        }
        out[i] = sum;
    }
}

std::vector<double> ReconstructFromLower(const CholeskyFactor& factor) {
    if (factor.n == 0 || factor.lower.size() != factor.n * factor.n) {
        return {};
    }
    std::vector<double> reconstructed(factor.n * factor.n, 0.0);
    for (std::size_t i = 0; i < factor.n; ++i) {
        for (std::size_t j = 0; j < factor.n; ++j) {
            double sum = 0.0;
            const std::size_t k_max = std::min(i, j);
            for (std::size_t k = 0; k <= k_max; ++k) {
                sum += LowerAt(factor, i, k) * LowerAt(factor, j, k);
            }
            reconstructed[(i * factor.n) + j] = sum;
        }
    }
    return reconstructed;
}

}  // namespace numeraire::quant
