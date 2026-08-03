#include <gtest/gtest.h>

#include <cmath>
#include <numeraire/quant/interest_rate_transforms.hpp>

using numeraire::quant::ContinuousZeroFromDiscountFactor;
using numeraire::quant::DiscountFactorFromContinuousZero;

TEST(InterestRateTransformsTest, DiscountFactorFromContinuousZero_Basic) {
    const double z = 0.05;
    const double t = 1.0;
    EXPECT_NEAR(DiscountFactorFromContinuousZero(z, t), std::exp(-0.05), 1e-12);
}

TEST(InterestRateTransformsTest, ContinuousZeroFromDiscountFactor_Basic) {
    const double df = std::exp(-0.05);
    const double t = 1.0;
    EXPECT_NEAR(ContinuousZeroFromDiscountFactor(df, t), 0.05, 1e-12);
}

TEST(InterestRateTransformsTest, ContinuousZero_DiscountFactor_RoundTrip) {
    const double z = 0.0375;
    const double t = 2.5;
    const double df = DiscountFactorFromContinuousZero(z, t);
    EXPECT_NEAR(ContinuousZeroFromDiscountFactor(df, t), z, 1e-12);
}

TEST(InterestRateTransformsTest, DiscountFactorFromContinuousZero_ZeroTimeIsOne) {
    EXPECT_NEAR(DiscountFactorFromContinuousZero(0.05, 0.0), 1.0, 1e-12);
}
