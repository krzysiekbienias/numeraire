#include <numeraire/database/trade_lifecycle.hpp>

#include <numeraire/database/sqlite_trade_leg_booking_repository.hpp>
#include <numeraire/database/trade_booking_rules.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/utils/exception.hpp>

#include <SQLiteCpp/SQLiteCpp.h>

#include <string>

namespace numeraire::database {
namespace {

[[nodiscard]] bool LooksIsoDate(const std::string_view s) {
    return s.size() == 10U && s[4] == '-' && s[7] == '-';
}

[[nodiscard]] std::vector<std::string> FindMaturedLiveTradeIds(SQLite::Database& db,
                                                               const std::string_view as_of_iso,
                                                               const std::optional<std::string_view> portfolio_id) {
    std::string sql =
            "SELECT t.trade_id "
            "FROM trades t "
            "INNER JOIN trade_legs tl ON tl.trade_id = t.trade_id "
            "INNER JOIN products p ON p.product_id = tl.product_id "
            "WHERE t.status = ? ";
    if (portfolio_id.has_value()) {
        sql += "AND t.portfolio_id = ? ";
    }
    sql += "GROUP BY t.trade_id "
           "HAVING MAX(p.expiry_date) < ? "
           "ORDER BY t.trade_id";

    SQLite::Statement st(db, sql);
    int bind = 1;
    st.bind(bind++, std::string(kTradeStatusLive));
    if (portfolio_id.has_value()) {
        st.bind(bind++, std::string(*portfolio_id));
    }
    st.bind(bind++, std::string(as_of_iso));

    std::vector<std::string> out;
    while (st.executeStep()) {
        out.push_back(st.getColumn(0).getString());
    }
    return out;
}

}  // namespace

TradeLifecycleResult ApplyTradeLifecycleAsOf(const std::string& database_file_path,
                                             const std::string_view as_of_iso,
                                             const std::optional<std::string_view> portfolio_id) {
    if (!LooksIsoDate(as_of_iso)) {
        throw ValidationError("ApplyTradeLifecycleAsOf: as_of must be YYYY-MM-DD.");
    }
    static_cast<void>(schedule::ParseIsoDate(std::string(as_of_iso)));

    TradeLifecycleResult result;
    try {
        SQLite::Database db(database_file_path, SQLite::OPEN_READWRITE);
        result.expired_trade_ids = FindMaturedLiveTradeIds(db, as_of_iso, portfolio_id);
        if (result.expired_trade_ids.empty()) {
            return result;
        }

        SqliteTradeLegBookingRepository booking_repo(database_file_path);
        for (const std::string& trade_id : result.expired_trade_ids) {
            booking_repo.SetTradeStatus(trade_id, std::string(kTradeStatusExpired));
        }
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"ApplyTradeLifecycleAsOf: "} + e.what());
    }
    return result;
}

}  // namespace numeraire::database
