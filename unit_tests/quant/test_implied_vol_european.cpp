#include <numeraire/enums/option_type.hpp>
#include <gtest/gtest.h>

#include <numeraire/quant/black_scholes_vanilla.hpp>
#include <numeraire/quant/implied_vol_european.hpp>

TEST(ImpliedVolEuropeanTest, RoundTripCallAtm) {
    constexpr double spot = 29125.0;
    constexpr double strike = 29200.0;
    constexpr double r = 0.03;
    constexpr double q = 0.0;
    constexpr double tau = 0.12;
    constexpr double sigma = 0.22;

    const double price =
            numeraire::quant::EuropeanVanillaPrice(numeraire::OptionType::kCall, spot, strike, r, q, sigma, tau);
    const auto iv =
            numeraire::quant::ImpliedVolEuropeanVanilla(numeraire::OptionType::kCall, price, spot, strike, r, q, tau);

    ASSERT_EQ(iv.status, numeraire::quant::ImpliedVolStatus::kOk);
    EXPECT_NEAR(iv.implied_vol, sigma, 1.0e-6);
}

TEST(ImpliedVolEuropeanTest, BelowIntrinsicRejected) {
    const auto iv =
            numeraire::quant::ImpliedVolEuropeanVanilla(numeraire::OptionType::kCall, 1.0, 100.0, 90.0, 0.03, 0.0, 0.5);
    EXPECT_EQ(iv.status, numeraire::quant::ImpliedVolStatus::kBelowIntrinsic);
}

TEST(ImpliedVolEuropeanTest, InvalidInputsRejected) {
    const auto iv = numeraire::quant::ImpliedVolEuropeanVanilla(
            numeraire::OptionType::kPut, -1.0, 100.0, 100.0, 0.03, 0.0, 0.5);
    EXPECT_EQ(iv.status, numeraire::quant::ImpliedVolStatus::kInvalidInputs);
}
