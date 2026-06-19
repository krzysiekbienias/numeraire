#include <numeraire/simulation/gbm_spec.hpp>

#include <numeraire/simulation/historical_calibrator.hpp>
#include <numeraire/utils/exception.hpp>

namespace numeraire::simulation {

MultiFactorGbmSpec BuildMultiFactorGbmSpec(const HistoricalCalibrationResult& calibration,
                                           const double risk_free_rate,
                                           const double dividend_yield) {
    const std::size_t n = calibration.factor_ids.size();
    if (n == 0) {
        throw ValidationError("BuildMultiFactorGbmSpec: calibration has no factors.");
    }
    if (calibration.spots_as_of.size() != n || calibration.volatilities.size() != n ||
        calibration.cholesky.n != n) {
        throw ValidationError("BuildMultiFactorGbmSpec: calibration vectors have inconsistent lengths.");
    }

    MultiFactorGbmSpec spec;
    spec.spots = calibration.spots_as_of;
    spec.volatilities = calibration.volatilities;
    spec.cholesky = calibration.cholesky;
    spec.risk_free_rates.assign(n, risk_free_rate);
    spec.dividend_yields.assign(n, dividend_yield);
    return spec;
}

}  // namespace numeraire::simulation
