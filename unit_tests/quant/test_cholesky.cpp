#include <gtest/gtest.h>

#include <cmath>
#include <numeraire/quant/cholesky.hpp>
#include <vector>

namespace {

using numeraire::quant::ApplyLowerTriangular;
using numeraire::quant::CholeskyDecompose;
using numeraire::quant::CholeskyStatus;
using numeraire::quant::ReconstructFromLower;

[[nodiscard]] bool MatricesNear(const std::vector<double>& a, const std::vector<double>& b,
                                const double tol) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::abs(a[i] - b[i]) > tol) {
            return false;
        }
    }
    return true;
}

TEST(CholeskyTest, IdentityTwoByTwo) {
    const std::vector<double> identity = {1.0, 0.0, 0.0, 1.0};
    const auto result = CholeskyDecompose(identity, 2);
    ASSERT_EQ(result.status, CholeskyStatus::kOk);
    ASSERT_EQ(result.factor.n, 2U);
    EXPECT_NEAR(result.factor.lower[0], 1.0, 1.0e-12);
    EXPECT_NEAR(result.factor.lower[1], 0.0, 1.0e-12);
    EXPECT_NEAR(result.factor.lower[2], 0.0, 1.0e-12);
    EXPECT_NEAR(result.factor.lower[3], 1.0, 1.0e-12);
}

TEST(CholeskyTest, TwoAssetCorrelationReconstructs) {
    constexpr double kRho = 0.5;
    const std::vector<double> corr = {1.0, kRho, kRho, 1.0};
    const auto result = CholeskyDecompose(corr, 2);
    ASSERT_EQ(result.status, CholeskyStatus::kOk);

    const double expected_l10 = kRho;
    const double expected_l11 = std::sqrt(1.0 - (kRho * kRho));
    EXPECT_NEAR(result.factor.lower[0], 1.0, 1.0e-12);
    EXPECT_NEAR(result.factor.lower[1], 0.0, 1.0e-12);
    EXPECT_NEAR(result.factor.lower[2], expected_l10, 1.0e-12);
    EXPECT_NEAR(result.factor.lower[3], expected_l11, 1.0e-12);

    const auto reconstructed = ReconstructFromLower(result.factor);
    EXPECT_TRUE(MatricesNear(reconstructed, corr, 1.0e-10));
}

TEST(CholeskyTest, ThreeAssetCorrelationReconstructs) {
    const std::vector<double> corr = {
            1.0, 0.3, 0.2,
            0.3, 1.0, 0.4,
            0.2, 0.4, 1.0,
    };
    const auto result = CholeskyDecompose(corr, 3);
    ASSERT_EQ(result.status, CholeskyStatus::kOk);
    const auto reconstructed = ReconstructFromLower(result.factor);
    EXPECT_TRUE(MatricesNear(reconstructed, corr, 1.0e-10));
}

TEST(CholeskyTest, ApplyLowerTriangular) {
    constexpr double kRho = 0.5;
    const std::vector<double> corr = {1.0, kRho, kRho, 1.0};
    const auto result = CholeskyDecompose(corr, 2);
    ASSERT_EQ(result.status, CholeskyStatus::kOk);

    const std::vector<double> z_in = {1.0, 0.0};
    std::vector<double> z_out(2, 0.0);
    ApplyLowerTriangular(result.factor, z_in, z_out);
    EXPECT_NEAR(z_out[0], 1.0, 1.0e-12);
    EXPECT_NEAR(z_out[1], kRho, 1.0e-12);
}

TEST(CholeskyTest, NonPositiveDefiniteFails) {
    const std::vector<double> singular = {1.0, 1.0, 1.0, 1.0};
    const auto result = CholeskyDecompose(singular, 2);
    EXPECT_EQ(result.status, CholeskyStatus::kNotPositiveDefinite);
}

TEST(CholeskyTest, InvalidInputsRejected) {
    const std::vector<double> corr = {1.0, 0.5, 0.5, 1.0};
    EXPECT_EQ(CholeskyDecompose(corr, 0).status, CholeskyStatus::kInvalidInputs);
    EXPECT_EQ(CholeskyDecompose(corr, 3).status, CholeskyStatus::kInvalidInputs);

    const std::vector<double> bad_diag = {1.1, 0.0, 0.0, 1.0};
    EXPECT_EQ(CholeskyDecompose(bad_diag, 2).status, CholeskyStatus::kInvalidInputs);

    const std::vector<double> asymmetric = {1.0, 0.2, 0.5, 1.0};
    EXPECT_EQ(CholeskyDecompose(asymmetric, 2).status, CholeskyStatus::kInvalidInputs);
}

}  // namespace
