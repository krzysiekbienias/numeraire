#include <gtest/gtest.h>

#include <numeraire/enums/option_type.hpp>
#include <numeraire/quant/black_scholes_vanilla.hpp>
#include <numeraire/utils/quantlib_bridge.hpp>

#include <ql/instruments/payoffs.hpp>
#include <ql/pricingengines/blackcalculator.hpp>

#include <cmath>

namespace {

using numeraire::OptionType;
using numeraire::quant::AssetOrNothingPrice;
using numeraire::quant::CashOrNothingPrice;
using numeraire::quant::EuropeanVanillaAllGreeks;
using numeraire::quant::EuropeanVanillaPrice;
using numeraire::quant::EuropeanVanillaVega;

constexpr double kSpot = 100.0;
constexpr double kStrike = 100.0;
constexpr double kRate = 0.05;
constexpr double kDiv = 0.02;
constexpr double kVol = 0.25;
constexpr double kTau = 1.0;

[[nodiscard]] double Forward(const double spot, const double r, const double q, const double tau) {
    return spot * std::exp((r - q) * tau);
}

[[nodiscard]] double Discount(const double r, const double tau) { return std::exp(-r * tau); }

/// Black-76 on the forward, which is the same model as Black–Scholes on spot.
[[nodiscard]] QuantLib::BlackCalculator BenchVanilla(const OptionType kind,
                                                     const double spot,
                                                     const double strike,
                                                     const double r,
                                                     const double q,
                                                     const double vol,
                                                     const double tau) {
    return QuantLib::BlackCalculator(numeraire::utils::quantlib_bridge::ToQuantLib(kind), strike,
                                     Forward(spot, r, q, tau), vol * std::sqrt(tau), Discount(r, tau));
}

/// Independent ground truth for binaries: QuantLib prices its own digital payoffs, so the
/// expectation does not re-derive our closed form.
[[nodiscard]] double BenchAssetOrNothing(const OptionType kind,
                                         const double spot,
                                         const double strike,
                                         const double r,
                                         const double q,
                                         const double vol,
                                         const double tau) {
    const auto payoff = QuantLib::ext::make_shared<QuantLib::AssetOrNothingPayoff>(
            numeraire::utils::quantlib_bridge::ToQuantLib(kind), strike);
    return QuantLib::BlackCalculator(payoff, Forward(spot, r, q, tau), vol * std::sqrt(tau),
                                     Discount(r, tau))
            .value();
}

[[nodiscard]] double BenchCashOrNothing(const OptionType kind,
                                        const double spot,
                                        const double strike,
                                        const double cash_payout,
                                        const double r,
                                        const double q,
                                        const double vol,
                                        const double tau) {
    const auto payoff = QuantLib::ext::make_shared<QuantLib::CashOrNothingPayoff>(
            numeraire::utils::quantlib_bridge::ToQuantLib(kind), strike, cash_payout);
    return QuantLib::BlackCalculator(payoff, Forward(spot, r, q, tau), vol * std::sqrt(tau),
                                     Discount(r, tau))
            .value();
}

}  // namespace

TEST(BlackScholesVanillaTest, PriceMatchesQuantLibForCallAndPut) {
    for (const OptionType kind : {OptionType::kCall, OptionType::kPut}) {
        const double actual = EuropeanVanillaPrice(kind, kSpot, kStrike, kRate, kDiv, kVol, kTau);
        const double expected = BenchVanilla(kind, kSpot, kStrike, kRate, kDiv, kVol, kTau).value();
        EXPECT_NEAR(actual, expected, 1.0e-12);
    }
}

TEST(BlackScholesVanillaTest, AllGreeksMatchQuantLibForCallAndPut) {
    for (const OptionType kind : {OptionType::kCall, OptionType::kPut}) {
        const auto actual = EuropeanVanillaAllGreeks(kind, kSpot, kStrike, kRate, kDiv, kVol, kTau);
        const QuantLib::BlackCalculator bench =
                BenchVanilla(kind, kSpot, kStrike, kRate, kDiv, kVol, kTau);

        EXPECT_NEAR(actual.delta, bench.delta(kSpot), 1.0e-10);
        EXPECT_NEAR(actual.gamma, bench.gamma(kSpot), 1.0e-10);
        EXPECT_NEAR(actual.vega, bench.vega(kTau), 1.0e-10);
        EXPECT_NEAR(actual.theta, bench.theta(kSpot, kTau), 1.0e-10);
        EXPECT_NEAR(actual.rho, bench.rho(kTau), 1.0e-10);
    }
}

TEST(BlackScholesVanillaTest, StandaloneVegaAgreesWithGreekBundle) {
    for (const OptionType kind : {OptionType::kCall, OptionType::kPut}) {
        const double standalone = EuropeanVanillaVega(kind, kSpot, kStrike, kRate, kDiv, kVol, kTau);
        const auto bundle = EuropeanVanillaAllGreeks(kind, kSpot, kStrike, kRate, kDiv, kVol, kTau);
        EXPECT_NEAR(standalone, bundle.vega, 1.0e-12);
    }
}

TEST(BlackScholesVanillaTest, AssetOrNothingMatchesQuantLibDigitalPayoff) {
    for (const OptionType kind : {OptionType::kCall, OptionType::kPut}) {
        for (const double strike : {90.0, 100.0, 110.0}) {
            const double actual = AssetOrNothingPrice(kind, kSpot, strike, kRate, kDiv, kVol, kTau);
            const double expected = BenchAssetOrNothing(kind, kSpot, strike, kRate, kDiv, kVol, kTau);
            EXPECT_NEAR(actual, expected, 1.0e-10) << "strike=" << strike;
        }
    }
}

TEST(BlackScholesVanillaTest, CashOrNothingMatchesQuantLibDigitalPayoff) {
    constexpr double kPayout = 7.5;
    for (const OptionType kind : {OptionType::kCall, OptionType::kPut}) {
        for (const double strike : {90.0, 100.0, 110.0}) {
            const double actual =
                    CashOrNothingPrice(kind, kSpot, strike, kPayout, kRate, kDiv, kVol, kTau);
            const double expected =
                    BenchCashOrNothing(kind, kSpot, strike, kPayout, kRate, kDiv, kVol, kTau);
            EXPECT_NEAR(actual, expected, 1.0e-10) << "strike=" << strike;
        }
    }
}

TEST(BlackScholesVanillaTest, ZeroTimeCollapsesToIntrinsic) {
    constexpr double kPayout = 3.0;

    EXPECT_DOUBLE_EQ(EuropeanVanillaPrice(OptionType::kCall, 105.0, 100.0, kRate, kDiv, kVol, 0.0), 5.0);
    EXPECT_DOUBLE_EQ(EuropeanVanillaPrice(OptionType::kPut, 105.0, 100.0, kRate, kDiv, kVol, 0.0), 0.0);

    EXPECT_DOUBLE_EQ(AssetOrNothingPrice(OptionType::kCall, 105.0, 100.0, kRate, kDiv, kVol, 0.0), 105.0);
    EXPECT_DOUBLE_EQ(AssetOrNothingPrice(OptionType::kCall, 95.0, 100.0, kRate, kDiv, kVol, 0.0), 0.0);

    EXPECT_DOUBLE_EQ(
            CashOrNothingPrice(OptionType::kCall, 105.0, 100.0, kPayout, kRate, kDiv, kVol, 0.0), kPayout);
    EXPECT_DOUBLE_EQ(
            CashOrNothingPrice(OptionType::kCall, 95.0, 100.0, kPayout, kRate, kDiv, kVol, 0.0), 0.0);
}

// QuantLib's BlackCalculator divides by zero at null variance, so the deterministic limit is
// hand-computed here: the spot arrives at its forward with certainty.
TEST(BlackScholesVanillaTest, ZeroVolCollapsesToDeterministicForwardLimit) {
    constexpr double kPayout = 3.0;
    const double forward = Forward(kSpot, kRate, kDiv, kTau);
    const double discount = Discount(kRate, kTau);
    ASSERT_GT(forward, kStrike) << "test relies on the forward finishing ITM for a call";

    EXPECT_DOUBLE_EQ(EuropeanVanillaPrice(OptionType::kCall, kSpot, kStrike, kRate, kDiv, 0.0, kTau),
                     discount * (forward - kStrike));
    EXPECT_DOUBLE_EQ(EuropeanVanillaPrice(OptionType::kPut, kSpot, kStrike, kRate, kDiv, 0.0, kTau), 0.0);

    EXPECT_DOUBLE_EQ(AssetOrNothingPrice(OptionType::kCall, kSpot, kStrike, kRate, kDiv, 0.0, kTau),
                     kSpot * std::exp(-kDiv * kTau));
    EXPECT_DOUBLE_EQ(AssetOrNothingPrice(OptionType::kPut, kSpot, kStrike, kRate, kDiv, 0.0, kTau), 0.0);

    EXPECT_DOUBLE_EQ(
            CashOrNothingPrice(OptionType::kCall, kSpot, kStrike, kPayout, kRate, kDiv, 0.0, kTau),
            kPayout * discount);
    EXPECT_DOUBLE_EQ(
            CashOrNothingPrice(OptionType::kPut, kSpot, kStrike, kPayout, kRate, kDiv, 0.0, kTau), 0.0);
}

TEST(BlackScholesVanillaTest, ZeroVolPriceIsContinuousWithSmallVol) {
    const double at_zero = EuropeanVanillaPrice(OptionType::kCall, kSpot, kStrike, kRate, kDiv, 0.0, kTau);
    const double near_zero =
            EuropeanVanillaPrice(OptionType::kCall, kSpot, kStrike, kRate, kDiv, 1.0e-8, kTau);
    EXPECT_NEAR(at_zero, near_zero, 1.0e-6);
}
