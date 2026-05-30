#include <SQLiteCpp/SQLiteCpp.h>

#include <numeraire/database/discount_curve_eod_read.hpp>
#include <numeraire/utils/exception.hpp>
#include <string>
#include <vector>

namespace numeraire::database {

bool HasDiscountCurveEod(const std::string& database_file_path,
                         const std::string_view curve_id,
                         const std::string_view as_of_iso_yyyy_mm_dd) {
    try {
        SQLite::Database db(database_file_path, SQLite::OPEN_READONLY);
        SQLite::Statement st(db, "SELECT 1 FROM discount_curve_eod WHERE curve_id = ? AND as_of = ? LIMIT 1");
        st.bind(1, std::string(curve_id));
        st.bind(2, std::string(as_of_iso_yyyy_mm_dd));
        return st.executeStep();
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"HasDiscountCurveEod: "} + e.what());
    }
}

std::optional<DiscountCurveEodRead> TryLoadLatestDiscountCurveEod(
        const std::string& database_file_path,
        const std::string_view curve_id,
        const std::string_view on_or_before_as_of_iso_yyyy_mm_dd) {
    try {
        SQLite::Database db(database_file_path, SQLite::OPEN_READONLY);
        SQLite::Statement hdr(db,
                              "SELECT as_of, interpolation_method FROM discount_curve_eod "
                              "WHERE curve_id = ? AND as_of <= ? ORDER BY as_of DESC LIMIT 1");
        hdr.bind(1, std::string(curve_id));
        hdr.bind(2, std::string(on_or_before_as_of_iso_yyyy_mm_dd));
        if (!hdr.executeStep()) {
            return std::nullopt;
        }

        DiscountCurveEodRead out{};
        out.curve_id = std::string(curve_id);
        out.as_of = hdr.getColumn(0).getString();
        out.interpolation_method = hdr.getColumn(1).getString();

        SQLite::Statement pts(db,
                              "SELECT tenor, time_years, zero_rate, discount_factor "
                              "FROM discount_curve_point_eod WHERE curve_id = ? AND as_of = ? "
                              "ORDER BY time_years, tenor");
        pts.bind(1, out.curve_id);
        pts.bind(2, out.as_of);
        while (pts.executeStep()) {
            out.pillars.push_back(quant::BootstrappedCurvePoint{
                    .tenor = pts.getColumn(0).getString(),
                    .time_years = pts.getColumn(1).getDouble(),
                    .zero_rate = pts.getColumn(2).getDouble(),
                    .discount_factor = pts.getColumn(3).getDouble(),
            });
        }
        if (out.pillars.empty()) {
            return std::nullopt;
        }
        return out;
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"TryLoadLatestDiscountCurveEod: "} + e.what());
    }
}

}  // namespace numeraire::database
