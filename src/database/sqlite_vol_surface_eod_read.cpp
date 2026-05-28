#include <numeraire/database/vol_surface_eod_read.hpp>

#include <numeraire/schedule/date.hpp>
#include <numeraire/utils/exception.hpp>

#include <SQLiteCpp/SQLiteCpp.h>

#include <cmath>
#include <string>

namespace numeraire::database {
namespace {

[[nodiscard]] VolSurfaceGridPoint MakeGridPoint(const double strike,
                                                const double spot_used,
                                                const double years_to_maturity,
                                                const double implied_vol) {
    VolSurfaceGridPoint pt{};
    pt.log_moneyness = std::log(strike / spot_used);
    pt.years_to_maturity = years_to_maturity;
    pt.implied_vol = implied_vol;
    return pt;
}

}  // namespace

VolSurfaceEodRead LoadVolSurfaceEod(const std::string& database_file_path,
                                      const std::string_view underlying_id,
                                      const std::string_view as_of_iso_yyyy_mm_dd,
                                      const std::string_view surface_kind) {
    try {
        SQLite::Database db(database_file_path, SQLite::OPEN_READONLY);

        SQLite::Statement hdr(
                db,
                "SELECT surface_id, spot_used, risk_free_rate, dividend_yield "
                "FROM vol_surface_eod WHERE underlying_id = ? AND as_of = ? AND surface_kind = ?");
        hdr.bind(1, std::string(underlying_id));
        hdr.bind(2, std::string(as_of_iso_yyyy_mm_dd));
        hdr.bind(3, std::string(surface_kind));
        if (!hdr.executeStep()) {
            throw ValidationError("vol surface: no header for underlying=" + std::string(underlying_id) +
                                  " as_of=" + std::string(as_of_iso_yyyy_mm_dd) +
                                  " surface_kind=" + std::string(surface_kind));
        }

        const long surface_id = hdr.getColumn(0).getInt64();
        const double spot_used = hdr.getColumn(1).getDouble();
        const double risk_free_rate = hdr.getColumn(2).getDouble();
        const double dividend_yield = hdr.getColumn(3).getDouble();

        VolSurfaceEodRead out{};
        out.underlying_id = std::string(underlying_id);
        out.as_of = std::string(as_of_iso_yyyy_mm_dd);
        out.surface_kind = std::string(surface_kind);
        out.valuation_date = schedule::ParseIsoDate(out.as_of);
        out.spot_used = spot_used;
        out.risk_free_rate = risk_free_rate;
        out.dividend_yield = dividend_yield;

        SQLite::Statement pts(
                db,
                "SELECT strike, years_to_maturity, contract_type, implied_vol "
                "FROM vol_surface_point_eod WHERE surface_id = ? AND quality = 'ok' "
                "ORDER BY years_to_maturity, strike, contract_type");
        pts.bind(1, surface_id);

        while (pts.executeStep()) {
            const double strike = pts.getColumn(0).getDouble();
            const double tau = pts.getColumn(1).getDouble();
            const std::string contract_type = pts.getColumn(2).getString();
            const double iv = pts.getColumn(3).getDouble();
            const VolSurfaceGridPoint grid_pt = MakeGridPoint(strike, spot_used, tau, iv);
            if (contract_type == "call") {
                out.call_points.push_back(grid_pt);
            } else if (contract_type == "put") {
                out.put_points.push_back(grid_pt);
            }
        }

        if (out.call_points.empty() && out.put_points.empty()) {
            throw ValidationError("vol surface: no ok points for surface_id=" + std::to_string(surface_id));
        }

        return out;
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"LoadVolSurfaceEod: "} + e.what());
    }
}

}  // namespace numeraire::database
