#include <gtest/gtest.h>

#include <cmath>
#include <numeraire/quant/cholesky.hpp>
#include <numeraire/quant/nearest_correlation.hpp>
#include <vector>

namespace {

using numeraire::quant::CholeskyDecompose;
using numeraire::quant::CholeskyStatus;
using numeraire::quant::NearestCorrelationHigham;
using numeraire::quant::NearestCorrelationStatus;
using numeraire::quant::ReconstructFromLower;

TEST(NearestCorrelationTest, ValidCorrelationUnchanged) {
    const std::vector<double> corr = {
            1.0, 0.3, 0.2,
            0.3, 1.0, 0.4,
            0.2, 0.4, 1.0,
    };
    const auto result = NearestCorrelationHigham(corr, 3);
    ASSERT_EQ(result.status, NearestCorrelationStatus::kOk);
    for (std::size_t i = 0; i < corr.size(); ++i) {
        EXPECT_NEAR(result.matrix[i], corr[i], 1.0e-6);
    }
    EXPECT_EQ(CholeskyDecompose(result.matrix, 3).status, CholeskyStatus::kOk);
}

TEST(NearestCorrelationTest, RepairsNonPsdTwoByTwo) {
    // Unit diagonal but negative eigenvalue 1 - 1.05 = -0.05.
    const std::vector<double> invalid = {1.0, 1.05, 1.05, 1.0};
    const auto result = NearestCorrelationHigham(invalid, 2);
    ASSERT_EQ(result.status, NearestCorrelationStatus::kOk);
    EXPECT_NEAR(result.matrix[0], 1.0, 1.0e-8);
    EXPECT_NEAR(result.matrix[3], 1.0, 1.0e-8);
    EXPECT_NEAR(result.matrix[1], result.matrix[2], 1.0e-8);
    EXPECT_LE(result.matrix[1], 1.0 + 1.0e-8);
    EXPECT_GE(result.matrix[1], -1.0 - 1.0e-8);
}

TEST(NearestCorrelationTest, RepairedMatrixIsSymmetricWithUnitDiagonal) {
    const std::vector<double> invalid = {
            1.0, 0.95, 0.90,
            0.95, 1.0, 0.95,
            0.90, 0.95, 1.0,
    };
    const auto result = NearestCorrelationHigham(invalid, 3);
    ASSERT_EQ(result.status, NearestCorrelationStatus::kOk);
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_NEAR(result.matrix[(i * 3) + i], 1.0, 1.0e-8);
        for (std::size_t j = 0; j < i; ++j) {
            EXPECT_NEAR(result.matrix[(i * 3) + j], result.matrix[(j * 3) + i], 1.0e-8);
        }
    }
}

TEST(NearestCorrelationTest, CholeskyWorksAfterRepairForMildPerturbation) {
    const std::vector<double> almost = {
            1.0, 0.92, 0.88,
            0.92, 1.0, 0.91,
            0.88, 0.91, 1.0,
    };
    const auto repaired = NearestCorrelationHigham(almost, 3);
    ASSERT_EQ(repaired.status, NearestCorrelationStatus::kOk);
    const auto chol = CholeskyDecompose(repaired.matrix, 3);
    EXPECT_EQ(chol.status, CholeskyStatus::kOk);
    const auto reconstructed = ReconstructFromLower(chol.factor);
    for (std::size_t i = 0; i < repaired.matrix.size(); ++i) {
        EXPECT_NEAR(reconstructed[i], repaired.matrix[i], 1.0e-6);
    }
}

TEST(NearestCorrelationTest, InvalidInputsRejected) {
    const std::vector<double> corr = {1.0, 0.5, 0.5, 1.0};
    EXPECT_EQ(NearestCorrelationHigham(corr, 0).status, NearestCorrelationStatus::kInvalidInputs);
    EXPECT_EQ(NearestCorrelationHigham(corr, 3).status, NearestCorrelationStatus::kInvalidInputs);

    const std::vector<double> asymmetric = {1.0, 0.2, 0.5, 1.0};
    EXPECT_EQ(NearestCorrelationHigham(asymmetric, 2).status, NearestCorrelationStatus::kInvalidInputs);
}

}  // namespace
