#include <numeraire/database/futures_daily_eod_lookup.hpp>
#include <numeraire/utils/exception.hpp>

#include <SQLiteCpp/SQLiteCpp.h>

#include <cmath>
#include <optional>
#include <string>
#include <string_view>

namespace numeraire::database {

std::optional<double> LookupFuturesDailySettlement(const std::string& database_file_path,
                                                   const std::string_view ticker,
                                                   const std::string_view as_of_iso_yyyy_mm_dd) {
    try {
        SQLite::Database db(database_file_path, SQLite::OPEN_READONLY);
        SQLite::Statement st(
                db,
                "SELECT settlement_price, close FROM futures_daily_eod "
                "WHERE UPPER(ticker) = UPPER(?) AND as_of = ? AND timespan = '1session' "
                "LIMIT 1");
        st.bind(1, std::string(ticker));
        st.bind(2, std::string(as_of_iso_yyyy_mm_dd));
        if (!st.executeStep()) {
            return std::nullopt;
        }
        if (!st.getColumn(0).isNull()) {
            const double settle = st.getColumn(0).getDouble();
            if (std::isfinite(settle)) {
                return settle;
            }
        }
        if (!st.getColumn(1).isNull()) {
            const double close = st.getColumn(1).getDouble();
            if (std::isfinite(close)) {
                return close;
            }
        }
        return std::nullopt;
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"futures_daily_eod lookup: "} + e.what());
    }
}

}  // namespace numeraire::database
