#include <numeraire/database/underlying_daily_closes.hpp>

#include <numeraire/utils/exception.hpp>

#include <SQLiteCpp/SQLiteCpp.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace numeraire::database {
namespace {

[[nodiscard]] std::optional<std::string> IndexDailyTickerForUnderlying(const std::string_view underlying_id) {
    if (underlying_id == "NDX") {
        return std::string{"I:NDX"};
    }
    return std::nullopt;
}

[[nodiscard]] std::vector<DailyCloseObservation> LoadEquityRange(SQLite::Database& db,
                                                                const std::string_view ticker,
                                                                const std::string_view from_iso,
                                                                const std::string_view to_iso,
                                                                const int adjusted) {
    SQLite::Statement st(
            db,
            "SELECT as_of, close FROM equity_daily_eod WHERE UPPER(ticker) = UPPER(?) AND as_of >= ? AND "
            "as_of <= ? AND timespan = '1d' AND adjusted = ? ORDER BY as_of ASC");
    st.bind(1, std::string(ticker));
    st.bind(2, std::string(from_iso));
    st.bind(3, std::string(to_iso));
    st.bind(4, adjusted);

    std::vector<DailyCloseObservation> out;
    while (st.executeStep()) {
        out.push_back(DailyCloseObservation{.as_of = st.getColumn(0).getString(), .close = st.getColumn(1).getDouble()});
    }
    return out;
}

[[nodiscard]] std::vector<DailyCloseObservation> LoadIndexRange(SQLite::Database& db,
                                                              const std::string_view ticker,
                                                              const std::string_view from_iso,
                                                              const std::string_view to_iso,
                                                              const int adjusted) {
    SQLite::Statement st(
            db,
            "SELECT as_of, close FROM index_daily_eod WHERE UPPER(ticker) = UPPER(?) AND as_of >= ? AND "
            "as_of <= ? AND timespan = '1d' AND adjusted = ? ORDER BY as_of ASC");
    st.bind(1, std::string(ticker));
    st.bind(2, std::string(from_iso));
    st.bind(3, std::string(to_iso));
    st.bind(4, adjusted);

    std::vector<DailyCloseObservation> out;
    while (st.executeStep()) {
        out.push_back(DailyCloseObservation{.as_of = st.getColumn(0).getString(), .close = st.getColumn(1).getDouble()});
    }
    return out;
}

}  // namespace

std::vector<DailyCloseObservation> LoadUnderlyingDailyClosesRange(const std::string& database_file_path,
                                                                const std::string_view underlying_id,
                                                                const std::string_view from_iso_yyyy_mm_dd,
                                                                const std::string_view to_iso_yyyy_mm_dd,
                                                                const int adjusted) {
    try {
        SQLite::Database db(database_file_path, SQLite::OPEN_READONLY);
        std::vector<DailyCloseObservation> equity =
                LoadEquityRange(db, underlying_id, from_iso_yyyy_mm_dd, to_iso_yyyy_mm_dd, adjusted);
        if (!equity.empty()) {
            return equity;
        }
        if (const std::optional<std::string> index_ticker = IndexDailyTickerForUnderlying(underlying_id);
            index_ticker.has_value()) {
            return LoadIndexRange(db, *index_ticker, from_iso_yyyy_mm_dd, to_iso_yyyy_mm_dd, adjusted);
        }
        return {};
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"underlying daily closes range: "} + e.what());
    }
}

std::vector<std::string> ListDistinctBookUnderlyingIds(const std::string& database_file_path,
                                                     const std::optional<std::string_view> trade_status,
                                                     const std::optional<std::string_view> portfolio_id) {
    try {
        SQLite::Database db(database_file_path, SQLite::OPEN_READONLY);
        std::string sql =
                "SELECT DISTINCT p.underlying_id FROM trade_legs tl "
                "JOIN products p ON tl.product_id = p.product_id "
                "JOIN trades t ON tl.trade_id = t.trade_id ";
        bool has_where = false;
        if (trade_status.has_value()) {
            sql += "WHERE t.status = ? ";
            has_where = true;
        }
        if (portfolio_id.has_value()) {
            sql += has_where ? "AND t.portfolio_id = ? " : "WHERE t.portfolio_id = ? ";
        }
        sql += "ORDER BY p.underlying_id ASC";

        SQLite::Statement st(db, sql);
        int bind_idx = 1;
        if (trade_status.has_value()) {
            st.bind(bind_idx++, std::string(*trade_status));
        }
        if (portfolio_id.has_value()) {
            st.bind(bind_idx, std::string(*portfolio_id));
        }

        std::vector<std::string> out;
        while (st.executeStep()) {
            out.emplace_back(st.getColumn(0).getString());
        }
        return out;
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"list book underlyings: "} + e.what());
    }
}

}  // namespace numeraire::database
