#include <gtest/gtest.h>

#include <numeraire/quant/gabillon_curve_fit.hpp>

#include <cmath>
#include <vector>

namespace {

using numeraire::quant::FitGabillonCurve;
using numeraire::quant::FitGabillonCurveGivenMeanReversion;
using numeraire::quant::FitGabillonVolatilities;
using numeraire::quant::ForwardCurvePoint;
using numeraire::quant::GabillonFitStatus;
using numeraire::quant::GabillonForwardVolatility;
using numeraire::quant::SeasonalShape;
using numeraire::quant::SeasonalValue;

/// Monthly contracts out `num_years` years, priced by the model itself so the fit has an
/// exact answer to find.
[[nodiscard]] std::vector<ForwardCurvePoint> SyntheticCurve(const double k, const double short_level,
                                                            const double long_level,
                                                            const SeasonalShape& seasonal,
                                                            const int num_years = 6) {
    std::vector<ForwardCurvePoint> curve;
    for (int month = 1; month <= 12 * num_years; ++month) {
        const double tau = static_cast<double>(month) / 12.0;
        const double phase = std::fmod(tau, 1.0);
        const double decay = std::exp(-k * tau);
        const double log_price = (decay * std::log(short_level)) + ((1.0 - decay) * std::log(long_level)) +
                                 SeasonalValue(seasonal, phase);
        curve.push_back(ForwardCurvePoint{
                .time_to_settlement_years = tau, .year_phase = phase, .price = std::exp(log_price)});
    }
    return curve;
}

}  // namespace

TEST(GabillonCurveFitTest, RecoversParametersFromItsOwnCurve) {
    const SeasonalShape seasonal{.cos_coeffs = {0.13, 0.05}, .sin_coeffs = {-0.07, 0.02}};
    const auto curve = SyntheticCurve(2.0, 2.8, 3.6, seasonal);

    const auto fit = FitGabillonCurve(curve, 2);
    ASSERT_EQ(fit.status, GabillonFitStatus::kOk);
    EXPECT_NEAR(fit.mean_reversion, 2.0, 1.0e-2);
    EXPECT_NEAR(fit.short_factor_level, 2.8, 1.0e-3);
    EXPECT_NEAR(fit.long_factor_level, 3.6, 1.0e-3);
    ASSERT_EQ(fit.seasonal.cos_coeffs.size(), 2U);
    EXPECT_NEAR(fit.seasonal.cos_coeffs[0], 0.13, 1.0e-3);
    EXPECT_NEAR(fit.seasonal.sin_coeffs[0], -0.07, 1.0e-3);
    EXPECT_NEAR(fit.seasonal.cos_coeffs[1], 0.05, 1.0e-3);
    EXPECT_NEAR(fit.seasonal.sin_coeffs[1], 0.02, 1.0e-3);
    EXPECT_LT(fit.log_rmse, 1.0e-4);
}

TEST(GabillonCurveFitTest, FindsNoSeasonalityInASmoothCurve) {
    // A curve with no annual cycle must not have one invented for it, otherwise every
    // non-seasonal commodity would pick up spurious winter risk.
    const auto curve = SyntheticCurve(1.5, 90.0, 62.0, SeasonalShape{});

    const auto fit = FitGabillonCurve(curve, 2);
    ASSERT_EQ(fit.status, GabillonFitStatus::kOk);
    for (const double c : fit.seasonal.cos_coeffs) {
        EXPECT_NEAR(c, 0.0, 1.0e-6);
    }
    for (const double s : fit.seasonal.sin_coeffs) {
        EXPECT_NEAR(s, 0.0, 1.0e-6);
    }
}

TEST(GabillonCurveFitTest, SeasonalShapeIsPeriodicAndMeanZero) {
    const SeasonalShape seasonal{.cos_coeffs = {0.13, 0.05}, .sin_coeffs = {-0.07, 0.02}};
    EXPECT_NEAR(SeasonalValue(seasonal, 0.25), SeasonalValue(seasonal, 1.25), 1.0e-12);

    double sum = 0.0;
    constexpr int kSamples = 2000;
    for (int i = 0; i < kSamples; ++i) {
        sum += SeasonalValue(seasonal, static_cast<double>(i) / kSamples);
    }
    EXPECT_NEAR(sum / kSamples, 0.0, 1.0e-9);
}

TEST(GabillonCurveFitTest, RejectsCurvesTooShortForTheHarmonicsRequested) {
    std::vector<ForwardCurvePoint> curve{
            ForwardCurvePoint{.time_to_settlement_years = 0.1, .year_phase = 0.1, .price = 3.0},
            ForwardCurvePoint{.time_to_settlement_years = 0.2, .year_phase = 0.2, .price = 3.1},
    };
    EXPECT_EQ(FitGabillonCurve(curve, 2).status, GabillonFitStatus::kInvalidInputs);
    EXPECT_EQ(FitGabillonCurveGivenMeanReversion(curve, 1.0, 2).status, GabillonFitStatus::kInvalidInputs);
}

TEST(GabillonCurveFitTest, ForwardVolatilityDecaysFromShortFactorToLongFactor) {
    constexpr double kDecay = 2.0;
    constexpr double kShortVol = 0.80;
    constexpr double kLongVol = 0.30;
    constexpr double kRho = 0.5;

    // At zero maturity only the short factor is live; far out, only the long factor.
    EXPECT_NEAR(GabillonForwardVolatility(0.0, kDecay, kShortVol, kLongVol, kRho), kShortVol, 1.0e-12);
    EXPECT_NEAR(GabillonForwardVolatility(50.0, kDecay, kShortVol, kLongVol, kRho), kLongVol, 1.0e-9);

    // Monotone decay in between is the Samuelson effect.
    double previous = GabillonForwardVolatility(0.0, kDecay, kShortVol, kLongVol, kRho);
    for (double tau = 0.1; tau <= 3.0; tau += 0.1) {
        const double current = GabillonForwardVolatility(tau, kDecay, kShortVol, kLongVol, kRho);
        EXPECT_LT(current, previous) << "tau=" << tau;
        previous = current;
    }
}

TEST(GabillonVolFitTest, RecoversVolatilityParametersFromItsOwnTermStructure) {
    constexpr double kDecay = 3.0;
    constexpr double kShortVol = 0.75;
    constexpr double kLongVol = 0.28;
    constexpr double kRho = 0.4;

    std::vector<double> tenors;
    std::vector<double> vols;
    for (int month = 1; month <= 24; ++month) {
        const double tau = static_cast<double>(month) / 12.0;
        tenors.push_back(tau);
        vols.push_back(GabillonForwardVolatility(tau, kDecay, kShortVol, kLongVol, kRho));
    }

    const auto fit = FitGabillonVolatilities(tenors, vols);
    ASSERT_EQ(fit.status, GabillonFitStatus::kOk);
    EXPECT_NEAR(fit.mean_reversion, kDecay, 1.0e-2);
    EXPECT_NEAR(fit.short_factor_vol, kShortVol, 1.0e-3);
    EXPECT_NEAR(fit.long_factor_vol, kLongVol, 1.0e-3);
    EXPECT_NEAR(fit.factor_correlation, kRho, 1.0e-2);
    EXPECT_LT(fit.rmse, 1.0e-5);
}

TEST(GabillonVolFitTest, RejectsMismatchedOrDegenerateInputs) {
    EXPECT_EQ(FitGabillonVolatilities({0.1, 0.2}, {0.5, 0.4, 0.3}).status, GabillonFitStatus::kInvalidInputs);
    EXPECT_EQ(FitGabillonVolatilities({0.1, 0.2}, {0.5, 0.4}).status, GabillonFitStatus::kInvalidInputs);
    EXPECT_EQ(FitGabillonVolatilities({0.1, 0.2, 0.3}, {0.5, 0.0, 0.3}).status,
              GabillonFitStatus::kInvalidInputs);
}
