#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace numeraire::quant {

enum class CholeskyStatus : std::uint8_t {
    kOk,
    kInvalidInputs,
    kNotPositiveDefinite,
};

/// Lower-triangular factor `L` with `R = L * L^T` (row-major `n x n`; entries above the
/// diagonal are zero).
struct CholeskyFactor {
    std::size_t n{0};
    std::vector<double> lower;
};

struct CholeskyResult {
    CholeskyStatus status{CholeskyStatus::kInvalidInputs};
    CholeskyFactor factor;
};

/// Cholesky decomposition of a symmetric positive-definite matrix `R` stored row-major
/// (`n * n` elements). For correlation matrices, diagonals must be `1` within `1e-8`.
[[nodiscard]] CholeskyResult CholeskyDecompose(std::span<const double> matrix_row_major,
                                               std::size_t n);

/// `out = L * in` where `L` is lower triangular (`factor.lower`, row-major).
/// `in` and `out` must have length `factor.n`.
void ApplyLowerTriangular(const CholeskyFactor& factor, std::span<const double> in,
                          std::span<double> out);

/// Reconstruct `R = L * L^T` from a lower factor (row-major output, `n * n`).
[[nodiscard]] std::vector<double> ReconstructFromLower(const CholeskyFactor& factor);

}  // namespace numeraire::quant
