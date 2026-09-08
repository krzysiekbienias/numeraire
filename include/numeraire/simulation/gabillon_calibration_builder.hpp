#pragma once

#include <numeraire/utils/config.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace numeraire::simulation {

struct GabillonCalibrationBuildParams {
    std::string database_file_path;
    /// `trades.portfolio_id`, or "ALL" for every portfolio.
    std::string scope_key{"ALL"};
    std::string as_of;
    std::string batch_run_id;
    int lookback_calendar_days{504};
    std::size_t min_return_observations{60};
    int vol_annualization_days{252};
    /// Maturities the vol term structure is measured at. These are observation points for
    /// a three-parameter fit rather than risk factors, so the strip wants to reach as deep
    /// as the contracts being simulated — otherwise sigma_long is an extrapolation.
    int commodity_pillars{24};
    int seasonal_harmonics{2};
};

/// What one calibrated curve contributed, for reporting.
struct GabillonCurveSummary {
    std::string product_code;
    double mean_reversion{0.0};
    double short_factor_vol{0.0};
    double long_factor_vol{0.0};
    double factor_correlation{0.0};
    double vol_rmse{0.0};
    std::size_t num_vol_pillars{0};
    double seasonal_amplitude{0.0};
    double curve_log_rmse{0.0};
    std::size_t num_curve_contracts{0};
};

struct GabillonCalibrationBuildStats {
    std::int64_t calibration_id{0};
    int num_factors{0};
    std::size_t num_return_observations{0};
    std::vector<GabillonCurveSummary> curves;
};

/// Calibrate a two-factor Gabillon model per commodity curve in the book and persist it
/// as `model=gabillon_2f, source=fit`.
///
/// Two things are estimated from different data, because they are visible in different
/// places. The *seasonal shape* comes from one day's forward curve, where a repeating
/// annual cycle is directly observable across the deferred years rather than having to be
/// inferred from a short history. The *dynamics* — mean reversion and the two factor vols
/// — come from the term structure of pillar volatilities, which is where the Samuelson
/// decay lives.
///
/// This replaces a wide empirical correlation matrix with a structural one: two factors
/// per curve reproduce the pillar-to-pillar correlations that GBM had to measure pairwise
/// and noisily. Within a curve the factor correlation comes from the volatility fit, so it
/// stays consistent with the vols it was fitted beside; across curves it is measured on
/// factor returns extracted from pillar history, since nothing but the data can say how
/// oil and gas move together.
[[nodiscard]] GabillonCalibrationBuildStats BuildGabillonCalibration(
        const GabillonCalibrationBuildParams& params);

void PrintGabillonCalibrationUsageLines();

[[nodiscard]] int TryRunGabillonCalibration(int argc, char** argv, const numeraire::utils::Config& cfg);

}  // namespace numeraire::simulation
