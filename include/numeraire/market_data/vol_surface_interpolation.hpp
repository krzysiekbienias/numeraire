#pragma once

#include <numeraire/database/vol_surface_eod_read.hpp>
#include <vector>

namespace numeraire::market_data {

/// Bilinear-style interpolation on sparse \((\ln(K/S), \tau)\) points.
/// Clamps to the convex hull edges in each dimension; needs at least one point.
[[nodiscard]] double InterpolateImpliedVol(const std::vector<database::VolSurfaceGridPoint>& points,
                                           double log_moneyness,
                                           double years_to_maturity);

}  // namespace numeraire::market_data
