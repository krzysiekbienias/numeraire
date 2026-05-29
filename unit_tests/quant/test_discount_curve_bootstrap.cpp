#include <gtest/gtest.h>

#include <cmath>
#include <numeraire/quant/discount_curve_bootstrap.hpp>
#include <vector>

namespace {

using numeraire::quant::BootstrapDiscountCurve;
using numeraire::quant::BootstrapStatus;
using numeraire::quant::CurvePillarKind;
using numeraire::quant::CurvePillarQuote;
using numeraire::quant::CurvePillarTimeYears;
using numeraire::quant::DepositZeroRateFromQuote;
using numeraire::quant::DiscountFactorFromZeroRate;

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

[[nodiscard]] double ParBondPv(const std::vector<numeraire::quant::BootstrappedCurvePoint>& pillars,
                               const double maturity_years,
                               const double quoted_rate,
                               const double terminal_zero_rate) {
    const double coupon = 0.5 * quoted_rate;
    const int periods = static_cast<int>(std::lround(maturity_years / 0.5));
    double pv = 0.0;
    for (int k = 1; k < periods; ++k) {
        const double t = 0.5 * static_cast<double>(k);
        double zero = terminal_zero_rate;
        for (std::size_t i = 0; i + 1 < pillars.size(); ++i) {
            if (t + 1.0e-12 >= pillars[i].time_years && t <= pillars[i + 1].time_years + 1.0e-12) {
                const double w = (t - pillars[i].time_years) / (pillars[i + 1].time_years - pillars[i].time_years);
                zero = pillars[i].zero_rate + (w * (pillars[i + 1].zero_rate - pillars[i].zero_rate));
                break;
            }
        }
        pv += coupon * DiscountFactorFromZeroRate(zero, t);
    }
    pv += (1.0 + coupon) * DiscountFactorFromZeroRate(terminal_zero_rate, maturity_years);
    return pv;
}

}  // namespace

TEST(DiscountCurveBootstrapTest, DepositSixMonthExample) {
    constexpr double kQuoted = 0.0379;
    constexpr int kTenorDays = 182;
    const double time_years = CurvePillarTimeYears(kTenorDays);

    const double zero_rate = DepositZeroRateFromQuote(kQuoted, time_years);
    const double discount_factor = 1.0 / (1.0 + (kQuoted * time_years));

    EXPECT_NEAR(time_years, 182.0 / 365.0, 1.0e-12);  // NOLINT(readability-magic-numbers)
    EXPECT_NEAR(discount_factor, 0.98145, 1.0e-4);
    EXPECT_NEAR(zero_rate, 0.03756, 1.0e-4);
    EXPECT_NEAR(DiscountFactorFromZeroRate(zero_rate, time_years), discount_factor, 1.0e-8);
}

TEST(DiscountCurveBootstrapTest, FullCurveBootstrapsAndPricesParBonds) {
    const auto result = BootstrapDiscountCurve(FredTreasuryParQuotes());
    ASSERT_EQ(result.status, BootstrapStatus::kOk);
    ASSERT_EQ(result.pillars.size(), 9U);

    const auto& quotes = FredTreasuryParQuotes();
    for (std::size_t i = 0; i < quotes.size(); ++i) {
        EXPECT_EQ(result.pillars[i].tenor, quotes[i].tenor);
        EXPECT_NEAR(result.pillars[i].time_years, CurvePillarTimeYears(quotes[i].tenor_days), 1.0e-12);
        EXPECT_NEAR(result.pillars[i].discount_factor,
                    DiscountFactorFromZeroRate(result.pillars[i].zero_rate, result.pillars[i].time_years),
                    1.0e-12);
    }

    for (std::size_t i = 4; i < quotes.size(); ++i) {
        const double maturity = CurvePillarTimeYears(quotes[i].tenor_days);
        const double pv = ParBondPv(result.pillars, maturity, quotes[i].quoted_rate, result.pillars[i].zero_rate);
        EXPECT_NEAR(pv, 1.0, 1.0e-8) << "par pricing failed at " << quotes[i].tenor;
    }
}

TEST(DiscountCurveBootstrapTest, UnsortedQuotesRejected) {
    std::vector<CurvePillarQuote> quotes = {
            {.tenor = "1Y", .tenor_days = 365, .kind = CurvePillarKind::kDeposit, .quoted_rate = 0.0368},
            {.tenor = "6M", .tenor_days = 182, .kind = CurvePillarKind::kDeposit, .quoted_rate = 0.0379},
    };
    const auto result = BootstrapDiscountCurve(quotes);
    EXPECT_EQ(result.status, BootstrapStatus::kInvalidInputs);
    EXPECT_TRUE(result.pillars.empty());
}

TEST(DiscountCurveBootstrapTest, EmptyQuotesRejected) {
    const auto result = BootstrapDiscountCurve({});
    EXPECT_EQ(result.status, BootstrapStatus::kInvalidInputs);
}
