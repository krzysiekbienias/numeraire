#include <gtest/gtest.h>

#include <numeraire/enums/exercise_style.hpp>
#include <numeraire/enums/option_type.hpp>
#include <numeraire/quant/black_scholes_vanilla.hpp>
#include <numeraire/quant/cox_ross_rubinstein.hpp>

#include <cmath>

namespace {

using numeraire::ExerciseStyle;
using numeraire::OptionType;
using numeraire::quant::CoxRossRubinsteinVanillaPrice;
using numeraire::quant::EuropeanVanillaPrice;

}  // namespace

TEST(CoxRossRubinsteinTest, EuropeanCallConvergesToBlackScholes) {
    constexpr double spot = 100.0;
    constexpr double strike = 100.0;
    constexpr double r = 0.05;
    constexpr double q = 0.02;
    constexpr double vol = 0.25;
    constexpr double tau = 1.0;

    const double bs = EuropeanVanillaPrice(OptionType::kCall, spot, strike, r, q, vol, tau);
    const double tree =
            CoxRossRubinsteinVanillaPrice(OptionType::kCall, ExerciseStyle::kEuropean, spot, strike, r, q, vol, tau,
                                          400);
    EXPECT_NEAR(tree, bs, 0.05);
}

TEST(CoxRossRubinsteinTest, EuropeanPutConvergesToBlackScholes) {
    constexpr double spot = 100.0;
    constexpr double strike = 100.0;
    constexpr double r = 0.05;
    constexpr double q = 0.0;
    constexpr double vol = 0.20;
    constexpr double tau = 0.5;

    const double bs = EuropeanVanillaPrice(OptionType::kPut, spot, strike, r, q, vol, tau);
    const double tree =
            CoxRossRubinsteinVanillaPrice(OptionType::kPut, ExerciseStyle::kEuropean, spot, strike, r, q, vol, tau,
                                          300);
    EXPECT_NEAR(tree, bs, 0.04);
}

TEST(CoxRossRubinsteinTest, AmericanPutHasEarlyExercisePremium) {
    constexpr double spot = 100.0;
    constexpr double strike = 100.0;
    constexpr double r = 0.05;
    constexpr double q = 0.0;
    constexpr double vol = 0.25;
    constexpr double tau = 1.0;
    constexpr std::size_t n = 200;

    const double european = CoxRossRubinsteinVanillaPrice(OptionType::kPut, ExerciseStyle::kEuropean, spot, strike, r,
                                                          q, vol, tau, n);
    const double american = CoxRossRubinsteinVanillaPrice(OptionType::kPut, ExerciseStyle::kAmerican, spot, strike, r,
                                                          q, vol, tau, n);
    EXPECT_GT(american, european + 0.05);
    EXPECT_GE(american, std::max(strike - spot, 0.0));
}

TEST(CoxRossRubinsteinTest, AmericanCallEqualsEuropeanWhenQIsZero) {
    constexpr double spot = 100.0;
    constexpr double strike = 100.0;
    constexpr double r = 0.05;
    constexpr double q = 0.0;
    constexpr double vol = 0.20;
    constexpr double tau = 1.0;
    constexpr std::size_t n = 150;

    const double european = CoxRossRubinsteinVanillaPrice(OptionType::kCall, ExerciseStyle::kEuropean, spot, strike, r,
                                                          q, vol, tau, n);
    const double american = CoxRossRubinsteinVanillaPrice(OptionType::kCall, ExerciseStyle::kAmerican, spot, strike, r,
                                                          q, vol, tau, n);
    EXPECT_NEAR(american, european, 1.0e-10);
}

TEST(CoxRossRubinsteinTest, ZeroTimeIsIntrinsic) {
    const double call = CoxRossRubinsteinVanillaPrice(OptionType::kCall, ExerciseStyle::kAmerican, 110.0, 100.0, 0.05,
                                                      0.0, 0.2, 0.0, 50);
    const double put = CoxRossRubinsteinVanillaPrice(OptionType::kPut, ExerciseStyle::kAmerican, 90.0, 100.0, 0.05, 0.0,
                                                     0.2, 0.0, 50);
    EXPECT_NEAR(call, 10.0, 1.0e-12);
    EXPECT_NEAR(put, 10.0, 1.0e-12);
}
