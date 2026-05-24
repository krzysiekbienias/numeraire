#include <SQLiteCpp/SQLiteCpp.h>

#include <array>
#include <chrono>
#include <memory>
#include <numeraire/database/sqlite_trade_leg_mtm_repository.hpp>
#include <numeraire/utils/exception.hpp>
#include <string>
#include <string_view>

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

constexpr const char* kArchiveInsertSql =
        "INSERT INTO trade_leg_mtm_eod_archive ("
        "batch_run_id, calculated_at, "
        "as_of, session_calendar, trade_id, leg_id, "
        "underlying_spot, risk_free_rate, dividend_yield, implied_vol_used, years_to_maturity, "
        "numeraire_currency, pv_unit, pv_total, pnl_daily, pnl_inception, "
        "delta, delta_total, gamma, gamma_total, vega, vega_total, "
        "theta, theta_total, rho, rho_total, "
        "pricing_engine, remarks"
        ") VALUES ("
        "?, ?, "
        "?, ?, ?, ?, "
        "?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?"
        ")";

constexpr const char* kUpsertSql =
        "INSERT OR REPLACE INTO trade_leg_mtm_eod ("
        "as_of, session_calendar, trade_id, leg_id, batch_run_id, "
        "underlying_spot, risk_free_rate, dividend_yield, implied_vol_used, years_to_maturity, "
        "numeraire_currency, pv_unit, pv_total, pnl_daily, pnl_inception, "
        "delta, delta_total, gamma, gamma_total, vega, vega_total, "
        "theta, theta_total, rho, rho_total, "
        "pricing_engine, calculated_at, remarks"
        ") VALUES ("
        "?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?"
        ")";

void BindMarketAndPvFields(SQLite::Statement& st, int& i, const TradeLegMtmEodRow& row) {
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
    st.bind(i++, row.delta_total);
    st.bind(i++, row.gamma);
    st.bind(i++, row.gamma_total);
    st.bind(i++, row.vega);
    st.bind(i++, row.vega_total);
    st.bind(i++, row.theta);
    st.bind(i++, row.theta_total);
    st.bind(i++, row.rho);
    st.bind(i++, row.rho_total);
}

}  // namespace

constexpr const char* kLookupPriorOfficialMarkSql =
        "SELECT as_of, pv_total FROM trade_leg_mtm_eod "
        "WHERE leg_id = ? AND pricing_engine = ? AND as_of < ? "
        "ORDER BY as_of DESC LIMIT 1";

struct SqliteTradeLegMtmRepository::Impl {
    std::unique_ptr<SQLite::Database> db;
    std::unique_ptr<SQLite::Statement> archive_insert;
    std::unique_ptr<SQLite::Statement> upsert;
    std::unique_ptr<SQLite::Statement> lookup_prior_official;
};

SqliteTradeLegMtmRepository::SqliteTradeLegMtmRepository(const std::string& database_file_path)
    : impl_(std::make_unique<Impl>()) {
    try {
        impl_->db =
                std::make_unique<SQLite::Database>(database_file_path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        impl_->db->exec("PRAGMA foreign_keys = ON;");
        impl_->archive_insert = std::make_unique<SQLite::Statement>(*impl_->db, kArchiveInsertSql);
        impl_->upsert = std::make_unique<SQLite::Statement>(*impl_->db, kUpsertSql);
        impl_->lookup_prior_official =
                std::make_unique<SQLite::Statement>(*impl_->db, kLookupPriorOfficialMarkSql);
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"SqliteTradeLegMtmRepository: "} + e.what());
    }
}

SqliteTradeLegMtmRepository::~SqliteTradeLegMtmRepository() = default;

void SqliteTradeLegMtmRepository::InsertArchive(const TradeLegMtmEodRow& row, const std::string& calculated_at) const {
    if (!row.batch_run_id.has_value() || row.batch_run_id->empty()) {
        throw ValidationError("TradeLegMtmEodRow: batch_run_id is required for archive insert");
    }

    try {
        SQLite::Statement& st = *impl_->archive_insert;
        st.reset();
        st.clearBindings();

        int i = 1;
        st.bind(i++, *row.batch_run_id);
        st.bind(i++, calculated_at);
        st.bind(i++, row.as_of);
        st.bind(i++, row.session_calendar);
        st.bind(i++, row.trade_id);
        st.bind(i++, row.leg_id);
        BindMarketAndPvFields(st, i, row);
        st.bind(i++, row.pricing_engine);
        st.bind(i++, row.remarks);

        st.exec();
    } catch (ValidationError const&) {
        throw;
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"SqliteTradeLegMtmRepository::InsertArchive: "} + e.what());
    }
}

void SqliteTradeLegMtmRepository::Upsert(const TradeLegMtmEodRow& row) const {
    if (row.as_of.empty() || row.trade_id.empty() || row.leg_id.empty() || row.pricing_engine.empty()) {
        throw ValidationError("TradeLegMtmEodRow: as_of, trade_id, leg_id, and pricing_engine must be non-empty");
    }

    const std::string calculated_at = row.calculated_at.empty() ? DefaultUtcTimestampIso8601() : row.calculated_at;

    if (row.batch_run_id.has_value() && !row.batch_run_id->empty()) {
        InsertArchive(row, calculated_at);
    }

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

        BindMarketAndPvFields(st, i, row);

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

std::optional<PriorOfficialMtmMark> SqliteTradeLegMtmRepository::LookupPriorOfficialMark(
        const std::string_view leg_id,
        const std::string_view pricing_engine,
        const std::string_view as_of) const {
    if (leg_id.empty() || pricing_engine.empty() || as_of.empty()) {
        throw ValidationError(
                "LookupPriorOfficialMark: leg_id, pricing_engine, and as_of must be non-empty");
    }

    try {
        SQLite::Statement& st = *impl_->lookup_prior_official;
        st.reset();
        st.clearBindings();
        st.bind(1, std::string(leg_id));
        st.bind(2, std::string(pricing_engine));
        st.bind(3, std::string(as_of));

        if (!st.executeStep()) {
            return std::nullopt;
        }

        PriorOfficialMtmMark mark{};
        mark.as_of = st.getColumn(0).getText();
        mark.pv_total = st.getColumn(1).getDouble();
        return mark;
    } catch (ValidationError const&) {
        throw;
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"SqliteTradeLegMtmRepository::LookupPriorOfficialMark: "} + e.what());
    }
}

}  // namespace numeraire::database
