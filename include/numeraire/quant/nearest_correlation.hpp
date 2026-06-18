#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace numeraire::quant {

enum class NearestCorrelationStatus : std::uint8_t {
    kOk,
    kInvalidInputs,
    kNoConvergence,
};

struct NearestCorrelationResult {
    NearestCorrelationStatus status{NearestCorrelationStatus::kInvalidInputs};
    std::size_t n{0};
    /// Nearest correlation matrix, row-major (`n * n`): unit diagonal, symmetric, PSD.
    std::vector<double> matrix;
    int iterations{0};
    /// Final Frobenius-norm step size `||Y - Y_prev||_F` when converged or stopped.
    double final_delta{0.0};
};

/// Higham alternating-projections nearest correlation matrix (2002).
/// Projects iteratively onto (unit diagonal) and (PSD) sets starting from a
/// symmetric input (typically a sample correlation estimate that may not be PSD).
[[nodiscard]] NearestCorrelationResult NearestCorrelationHigham(
        std::span<const double> matrix_row_major,
        std::size_t n,
        int max_iterations = 100,
        double tolerance = 1.0e-8);

}  // namespace numeraire::quant
