#include <numeraire/market_data/vol_surface_interpolation.hpp>

#include <gtest/gtest.h>

#include <vector>

using numeraire::database::VolSurfaceGridPoint;
using numeraire::market_data::InterpolateImpliedVol;

namespace {

std::vector<VolSurfaceGridPoint> MakeSmileSlice(const double tau) {
    return {
            VolSurfaceGridPoint{.log_moneyness = -0.10, .years_to_maturity = tau, .implied_vol = 0.24},
            VolSurfaceGridPoint{.log_moneyness = 0.00, .years_to_maturity = tau, .implied_vol = 0.20},
            VolSurfaceGridPoint{.log_moneyness = 0.10, .years_to_maturity = tau, .implied_vol = 0.22},
    };
}

}  // namespace

TEST(VolSurfaceInterpolationTest, ExactGridPointReturnsSameVol) {
    const std::vector<VolSurfaceGridPoint> pts = MakeSmileSlice(0.25);
    EXPECT_NEAR(InterpolateImpliedVol(pts, 0.00, 0.25), 0.20, 1.0e-12);
}

TEST(VolSurfaceInterpolationTest, LinearAlongLogMoneyness) {
    const std::vector<VolSurfaceGridPoint> pts = MakeSmileSlice(0.50);
    const double iv = InterpolateImpliedVol(pts, 0.05, 0.50);
    EXPECT_NEAR(iv, 0.21, 1.0e-12);
}

TEST(VolSurfaceInterpolationTest, LinearAlongTenor) {
    std::vector<VolSurfaceGridPoint> pts;
    for (const auto& p : MakeSmileSlice(0.25)) {
        pts.push_back(p);
    }
    for (const auto& p : MakeSmileSlice(0.75)) {
        pts.push_back(p);
    }
    const double iv = InterpolateImpliedVol(pts, 0.00, 0.50);
    EXPECT_NEAR(iv, 0.20, 1.0e-12);
}

TEST(VolSurfaceInterpolationTest, ClampsBeyondWing) {
    const std::vector<VolSurfaceGridPoint> pts = MakeSmileSlice(0.30);
    EXPECT_NEAR(InterpolateImpliedVol(pts, -0.50, 0.30), 0.24, 1.0e-12);
    EXPECT_NEAR(InterpolateImpliedVol(pts, 0.50, 0.30), 0.22, 1.0e-12);
}
