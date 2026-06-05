#include <gtest/gtest.h>

#include <cmath>
#include <numeraire/quant/discount_curve_bootstrap.hpp>
#include <numeraire/quant/fixed_income_pricing.hpp>
#include <vector>

namespace {

using numeraire::quant::BootstrapDiscountCurve;
using numeraire::quant::BootstrapStatus;
using numeraire::quant::BootstrappedCurvePoint;
using numeraire::quant::CurvePillarKind;
using numeraire::quant::CurvePillarQuote;
using numeraire::quant::CurvePillarTimeYears;
using numeraire::quant::DiscountFactorAtTime;
using numeraire::quant::DiscountFactorFromZeroRate;
using numeraire::quant::FixedCouponBondPv;
using numeraire::quant::FixedIncomeCashflow;
using numeraire::quant::FraNpv;
using numeraire::quant::InterpolateZeroRateAtTime;
using numeraire::quant::SimpleForwardRateFromDiscountFactors;
using numeraire::quant::ZeroCouponBondPv;

std::vector<CurvePillarQuote> FredTreasuryParQuotes() {
    return {
            {.tenor = "1M", .tenor_days = 30, .kind = CurvePillarKind::kDeposit, .quoted_rate = 0.0411},
            {.tenor = "3M", .tenor_days = 91, .kind = CurvePillarKind::kDeposit, .quoted_rate = 0.0410},
            {.tenor = "6M", .tenor_days = 182, .kind = CurvePillarKind::kDeposit, .quoted_rate = 0.0379},
            {.tenor = "1Y", .tenor_days = 365, .kind = CurvePillarKind::kDeposit, .quoted_rate = 0.0368},
            {.tenor = "2Y", .tenor_days = 730, .kind = CurvePillarKind::kSwap, .quoted_rate = 0.0400},
            {.tenor = "3Y", .tenor_days = 1095, .kind = CurvePillarKind::kSwap, .quoted_rate = 0.0395},
            {.tenor = "5Y", .tenor_days = 1825, .kind = CurvePillarKind::kSwap, .quoted_rate = 0.0415},
            {.tenor = "7Y", .tenor_days = 2555, .kind = CurvePillarKind::kSwap, .quoted_rate = 0.0435},
            {.tenor = "10Y", .tenor_days = 3650, .kind = CurvePillarKind::kSwap, .quoted_rate = 0.0455},
    };
}

[[nodiscard]] std::vector<FixedIncomeCashflow> SemiAnnualParBondCashflows(const double annual_coupon_rate,
                                                                            const double maturity_years) {
    std::vector<FixedIncomeCashflow> cashflows;
    const double coupon = 0.5 * annual_coupon_rate;
    const int periods = static_cast<int>(std::lround(maturity_years / 0.5));
    cashflows.reserve(static_cast<std::size_t>(periods));
    for (int k = 1; k <= periods; ++k) {
        const double time_years = 0.5 * static_cast<double>(k);
        const double amount = (k == periods) ? (1.0 + coupon) : coupon;
        cashflows.push_back({.time_years = time_years, .amount = amount});
    }
    return cashflows;
}

[[nodiscard]] std::vector<BootstrappedCurvePoint> FlatCurvePillars(const double zero_rate) {
    return {
            {.tenor = "1Y", .time_years = 1.0, .zero_rate = zero_rate, .discount_factor = std::exp(-zero_rate)},
            {.tenor = "5Y", .time_years = 5.0, .zero_rate = zero_rate, .discount_factor = std::exp(-5.0 * zero_rate)},
    };
}

}  // namespace

TEST(FixedIncomePricingTest, DiscountFactorAtTimeMatchesCurvePrimitives) {
    const auto result = BootstrapDiscountCurve(FredTreasuryParQuotes());
    ASSERT_EQ(result.status, BootstrapStatus::kOk);

    constexpr double kTimeYears = 2.25;
    const double zero_rate = InterpolateZeroRateAtTime(result.pillars, kTimeYears);
    const double expected = DiscountFactorFromZeroRate(zero_rate, kTimeYears);
    const double actual = DiscountFactorAtTime(result.pillars, kTimeYears);

    EXPECT_NEAR(actual, expected, 1.0e-12);
}

TEST(FixedIncomePricingTest, DiscountFactorAtTimeMatchesRiskFreeRateIdentity) {
    const auto result = BootstrapDiscountCurve(FredTreasuryParQuotes());
    ASSERT_EQ(result.status, BootstrapStatus::kOk);

    constexpr double kTimeYears = 3.75;
    const double zero_rate = InterpolateZeroRateAtTime(result.pillars, kTimeYears);
    const double from_rate = DiscountFactorFromZeroRate(zero_rate, kTimeYears);
    const double from_helper = DiscountFactorAtTime(result.pillars, kTimeYears);

    EXPECT_NEAR(from_helper, from_rate, 1.0e-12);
    EXPECT_NEAR(from_helper, std::exp(-zero_rate * kTimeYears), 1.0e-12);
}

TEST(FixedIncomePricingTest, ZeroCouponBondPvOnFlatCurve) {
    constexpr double kZeroRate = 0.04;
    constexpr double kMaturityYears = 2.0;
    constexpr double kNotional = 1'000'000.0;
    const auto pillars = FlatCurvePillars(kZeroRate);

    const double expected = kNotional * std::exp(-kZeroRate * kMaturityYears);
    const double actual = ZeroCouponBondPv(pillars, kNotional, kMaturityYears);

    EXPECT_NEAR(actual, expected, 1.0e-6);
}

TEST(FixedIncomePricingTest, ZeroCouponBondPvMatchesDiscountFactorHelper) {
    const auto result = BootstrapDiscountCurve(FredTreasuryParQuotes());
    ASSERT_EQ(result.status, BootstrapStatus::kOk);

    constexpr double kNotional = 1.0;
    constexpr double kMaturityYears = 5.0;
    const double expected = kNotional * DiscountFactorAtTime(result.pillars, kMaturityYears);
    const double actual = ZeroCouponBondPv(result.pillars, kNotional, kMaturityYears);

    EXPECT_NEAR(actual, expected, 1.0e-12);
}

TEST(FixedIncomePricingTest, FixedCouponBondPvPricesParBondsNearUnity) {
    const auto result = BootstrapDiscountCurve(FredTreasuryParQuotes());
    ASSERT_EQ(result.status, BootstrapStatus::kOk);

    const auto& quotes = FredTreasuryParQuotes();
    for (std::size_t i = 4; i < quotes.size(); ++i) {
        const double maturity_years = CurvePillarTimeYears(quotes[i].tenor_days);
        const auto cashflows = SemiAnnualParBondCashflows(quotes[i].quoted_rate, maturity_years);
        const double pv = FixedCouponBondPv(result.pillars, cashflows);
        EXPECT_NEAR(pv, 1.0, 1.0e-8) << "par pricing failed at " << quotes[i].tenor;
    }
}

TEST(FixedIncomePricingTest, FraNpvIsZeroWhenFixedEqualsForward) {
    constexpr double kZeroRate = 0.035;
    const auto pillars = FlatCurvePillars(kZeroRate);

    constexpr double kAccrualStart = 0.5;
    constexpr double kAccrualEnd = 1.0;
    constexpr double kPayment = 1.0;
    const double accrual = kAccrualEnd - kAccrualStart;

    const double df_start = DiscountFactorAtTime(pillars, kAccrualStart);
    const double df_end = DiscountFactorAtTime(pillars, kAccrualEnd);
    const double forward = SimpleForwardRateFromDiscountFactors(df_start, df_end, accrual);

    const double npv = FraNpv(pillars, 10'000'000.0, forward, kAccrualStart, kAccrualEnd, kPayment);
    EXPECT_NEAR(npv, 0.0, 1.0e-8);
}

TEST(FixedIncomePricingTest, FraNpvHandCheckOnFlatCurve) {
    constexpr double kZeroRate = 0.04;
    const auto pillars = FlatCurvePillars(kZeroRate);

    constexpr double kNotional = 5'000'000.0;
    constexpr double kFixedRate = 0.045;
    constexpr double kAccrualStart = 0.25;
    constexpr double kAccrualEnd = 0.75;
    constexpr double kPayment = 0.75;
    const double accrual = kAccrualEnd - kAccrualStart;

    const double df_start = std::exp(-kZeroRate * kAccrualStart);
    const double df_end = std::exp(-kZeroRate * kAccrualEnd);
    const double forward = SimpleForwardRateFromDiscountFactors(df_start, df_end, accrual);
    const double df_payment = std::exp(-kZeroRate * kPayment);
    const double expected = kNotional * accrual * df_payment * (kFixedRate - forward);

    const double actual = FraNpv(pillars, kNotional, kFixedRate, kAccrualStart, kAccrualEnd, kPayment);
    EXPECT_NEAR(actual, expected, 1.0e-6);
}

TEST(FixedIncomePricingTest, InvalidInputsReturnNaN) {
    const auto pillars = FlatCurvePillars(0.03);
    EXPECT_TRUE(std::isnan(ZeroCouponBondPv({}, 1.0, 1.0)));
    EXPECT_TRUE(std::isnan(ZeroCouponBondPv(pillars, 1.0, -0.1)));
    EXPECT_TRUE(std::isnan(FixedCouponBondPv(pillars, {})));
    EXPECT_TRUE(std::isnan(FraNpv(pillars, 1.0, 0.03, 1.0, 0.5, 1.0)));
}
