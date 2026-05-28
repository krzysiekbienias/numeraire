#pragma once

#include <numeraire/schedule/date.hpp>

#include <string>
#include <vector>

namespace numeraire::database {

/// One implied-vol grid point in \((\ln(K/S), \tau)\) coordinates.
struct VolSurfaceGridPoint {
    double log_moneyness{0.0};
    double years_to_maturity{0.0};
    double implied_vol{0.0};
};

/// EOD vol surface loaded from `vol_surface_eod` / `vol_surface_point_eod`.
struct VolSurfaceEodRead {
    std::string underlying_id;
    std::string as_of;
    std::string surface_kind;
    schedule::Date valuation_date{};
    double spot_used{0.0};
    double risk_free_rate{0.0};
    double dividend_yield{0.0};
    std::vector<VolSurfaceGridPoint> call_points;
    std::vector<VolSurfaceGridPoint> put_points;
};

/// Loads header + ok-quality points. Throws `ValidationError` if surface missing.
[[nodiscard]] VolSurfaceEodRead LoadVolSurfaceEod(const std::string& database_file_path,
                                                    std::string_view underlying_id,
                                                    std::string_view as_of_iso_yyyy_mm_dd,
                                                    std::string_view surface_kind = "implied_bs_eod");

}  // namespace numeraire::database
