#include <numeraire/database/index_daily_eod_lookup.hpp>
#include <numeraire/utils/exception.hpp>

#include <SQLiteCpp/SQLiteCpp.h>

namespace numeraire::database {

std::optional<double> LookupIndexDailyClose(const std::string& database_file_path,
                                            const std::string_view ticker,
                                            const std::string_view as_of_iso_yyyy_mm_dd,
                                            const int adjusted) {
    try {
        SQLite::Database db(database_file_path, SQLite::OPEN_READONLY);
        SQLite::Statement st(
                db,
                "SELECT close FROM index_daily_eod WHERE UPPER(ticker) = UPPER(?) AND as_of = ? AND "
                "timespan = '1d' AND adjusted = ? LIMIT 1");
        st.bind(1, std::string(ticker));
        st.bind(2, std::string(as_of_iso_yyyy_mm_dd));
        st.bind(3, adjusted);
        if (!st.executeStep()) {
            return std::nullopt;
        }
        return st.getColumn(0).getDouble();
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"index_daily_eod lookup: "} + e.what());
    }
}

}  // namespace numeraire::database
