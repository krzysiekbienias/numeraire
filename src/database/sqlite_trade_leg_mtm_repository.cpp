#include <numeraire/database/sqlite_trade_leg_mtm_repository.hpp>

#include <numeraire/utils/exception.hpp>

#include <SQLiteCpp/SQLiteCpp.h>

#include <array>
#include <chrono>
#include <memory>
#include <string>

namespace numeraire::database {

namespace {

[[nodiscard]] std::string DefaultUtcTimestampIso8601() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    gmtime_s(&tm_buf, &t);
#else
    gmtime_r(&t, &tm_buf);
#endif
    std::array<char, 32> buf{};
    if (std::strftime(buf.data(), buf.size(), "%Y-%m-%dT%H:%M:%SZ", &tm_buf) == 0U) {
        return "1970-01-01T00:00:00Z";
    }
    return std::string(buf.data());
}

constexpr const char* kUpsertSql =
        "INSERT OR REPLACE INTO trade_leg_mtm_eod ("
        "as_of, session_calendar, trade_id, leg_id, batch_run_id, "
        "underlying_spot, risk_free_rate, dividend_yield, implied_vol_used, years_to_maturity, "
        "numeraire_currency, pv_unit, pv_total, pnl_daily, pnl_inception, "
        "delta, gamma, vega, theta, rho, "
        "pricing_engine, calculated_at, remarks"
        ") VALUES ("
        "?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?, "
        "?, ?, ?"
        ")";

}  // namespace

struct SqliteTradeLegMtmRepository::Impl {
    std::unique_ptr<SQLite::Database> db;
    std::unique_ptr<SQLite::Statement> upsert;
};

SqliteTradeLegMtmRepository::SqliteTradeLegMtmRepository(const std::string& database_file_path)
        : impl_(std::make_unique<Impl>()) {
    try {
        impl_->db = std::make_unique<SQLite::Database>(
                database_file_path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        impl_->db->exec("PRAGMA foreign_keys = ON;");
        impl_->upsert = std::make_unique<SQLite::Statement>(*impl_->db, kUpsertSql);
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"SqliteTradeLegMtmRepository: "} + e.what());
    }
}

SqliteTradeLegMtmRepository::~SqliteTradeLegMtmRepository() = default;

void SqliteTradeLegMtmRepository::Upsert(const TradeLegMtmEodRow& row) const {
    if (row.as_of.empty() || row.trade_id.empty() || row.leg_id.empty() || row.pricing_engine.empty()) {
        throw ValidationError(
                "TradeLegMtmEodRow: as_of, trade_id, leg_id, and pricing_engine must be non-empty");
    }

    const std::string calculated_at =
            row.calculated_at.empty() ? DefaultUtcTimestampIso8601() : row.calculated_at;

    try {
        SQLite::Statement& st = *impl_->upsert;
        st.reset();
        st.clearBindings();

        int i = 1;
        st.bind(i++, row.as_of);
        st.bind(i++, row.session_calendar);
        st.bind(i++, row.trade_id);
        st.bind(i++, row.leg_id);
        if (row.batch_run_id.has_value() && !row.batch_run_id->empty()) {
            st.bind(i++, *row.batch_run_id);
        } else {
            st.bind(i++);
        }

        st.bind(i++, row.underlying_spot);
        st.bind(i++, row.risk_free_rate);
        st.bind(i++, row.dividend_yield);
        st.bind(i++, row.implied_vol_used);
        st.bind(i++, row.years_to_maturity);

        st.bind(i++, row.numeraire_currency);
        st.bind(i++, row.pv_unit);
        st.bind(i++, row.pv_total);

        if (row.pnl_daily.has_value()) {
            st.bind(i++, *row.pnl_daily);
        } else {
            st.bind(i++);
        }
        if (row.pnl_inception.has_value()) {
            st.bind(i++, *row.pnl_inception);
        } else {
            st.bind(i++);
        }

        st.bind(i++, row.delta);
        st.bind(i++, row.gamma);
        st.bind(i++, row.vega);
        st.bind(i++, row.theta);
        st.bind(i++, row.rho);

        st.bind(i++, row.pricing_engine);
        st.bind(i++, calculated_at);
        st.bind(i++, row.remarks);

        st.exec();
    } catch (ValidationError const&) {
        throw;
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"SqliteTradeLegMtmRepository::Upsert: "} + e.what());
    }
}

}  // namespace numeraire::database
