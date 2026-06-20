#include <SQLiteCpp/SQLiteCpp.h>

#include <memory>
#include <numeraire/database/sqlite_trade_leg_booking_repository.hpp>
#include <numeraire/utils/exception.hpp>
#include <string>

namespace numeraire::database {

namespace {

constexpr const char* kUpdateLegByLegIdSql =
        "UPDATE trade_legs SET execution_price = ? WHERE leg_id = ?";

constexpr const char* kUpdateLegByTradeAndLegSql =
        "UPDATE trade_legs SET execution_price = ? WHERE leg_id = ? AND trade_id = ?";

constexpr const char* kSetBookingTimestampSql =
        "UPDATE trades SET booking_timestamp = ? WHERE trade_id = ?";

constexpr const char* kSetBookingTimestampNowSql =
        "UPDATE trades SET booking_timestamp = datetime('now') WHERE trade_id = ?";

constexpr const char* kSetTradeStatusSql =
        "UPDATE trades SET status = ?, updated_at = datetime('now') WHERE trade_id = ?";

void RequireNonEmpty(std::string_view value, const char* label) {
    if (value.empty()) {
        throw ValidationError(std::string{label} + ": must be non-empty");
    }
}

void RequireNonNegativeExecutionPrice(const double execution_price, const std::string_view leg_id) {
    if (execution_price < 0.0) {
        throw ValidationError("execution_price must be non-negative for leg " + std::string{leg_id});
    }
}

void RequireRowsChanged(const int changes, const std::string& context) {
    if (changes != 1) {
        throw PersistenceError(context + ": expected 1 row updated, got " + std::to_string(changes));
    }
}

}  // namespace

struct SqliteTradeLegBookingRepository::Impl {
    std::unique_ptr<SQLite::Database> db;
    std::unique_ptr<SQLite::Statement> update_leg_by_leg_id;
    std::unique_ptr<SQLite::Statement> update_leg_by_trade_and_leg;
    std::unique_ptr<SQLite::Statement> set_booking_timestamp;
    std::unique_ptr<SQLite::Statement> set_booking_timestamp_now;
    std::unique_ptr<SQLite::Statement> set_trade_status;
};

SqliteTradeLegBookingRepository::SqliteTradeLegBookingRepository(const std::string& database_file_path)
    : impl_(std::make_unique<Impl>()) {
    try {
        impl_->db =
                std::make_unique<SQLite::Database>(database_file_path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        impl_->db->exec("PRAGMA foreign_keys = ON;");
        impl_->update_leg_by_leg_id =
                std::make_unique<SQLite::Statement>(*impl_->db, kUpdateLegByLegIdSql);
        impl_->update_leg_by_trade_and_leg =
                std::make_unique<SQLite::Statement>(*impl_->db, kUpdateLegByTradeAndLegSql);
        impl_->set_booking_timestamp =
                std::make_unique<SQLite::Statement>(*impl_->db, kSetBookingTimestampSql);
        impl_->set_booking_timestamp_now =
                std::make_unique<SQLite::Statement>(*impl_->db, kSetBookingTimestampNowSql);
        impl_->set_trade_status = std::make_unique<SQLite::Statement>(*impl_->db, kSetTradeStatusSql);
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"SqliteTradeLegBookingRepository: "} + e.what());
    }
}

SqliteTradeLegBookingRepository::~SqliteTradeLegBookingRepository() = default;

void SqliteTradeLegBookingRepository::UpdateExecutionPrice(const std::string_view leg_id,
                                                           const double execution_price) const {
    RequireNonEmpty(leg_id, "leg_id");
    RequireNonNegativeExecutionPrice(execution_price, leg_id);

    try {
        SQLite::Statement& st = *impl_->update_leg_by_leg_id;
        st.reset();
        st.clearBindings();
        st.bind(1, execution_price);
        st.bind(2, std::string{leg_id});
        st.exec();
        RequireRowsChanged(st.getChanges(), "SqliteTradeLegBookingRepository::UpdateExecutionPrice");
    } catch (ValidationError const&) {
        throw;
    } catch (PersistenceError const&) {
        throw;
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"SqliteTradeLegBookingRepository::UpdateExecutionPrice: "} + e.what());
    }
}

void SqliteTradeLegBookingRepository::UpdateExecutionPriceForTrade(const std::string_view trade_id,
                                                                   const std::string_view leg_id,
                                                                   const double execution_price) const {
    RequireNonEmpty(trade_id, "trade_id");
    RequireNonEmpty(leg_id, "leg_id");
    RequireNonNegativeExecutionPrice(execution_price, leg_id);

    try {
        SQLite::Statement& st = *impl_->update_leg_by_trade_and_leg;
        st.reset();
        st.clearBindings();
        st.bind(1, execution_price);
        st.bind(2, std::string{leg_id});
        st.bind(3, std::string{trade_id});
        st.exec();
        RequireRowsChanged(st.getChanges(),
                           "SqliteTradeLegBookingRepository::UpdateExecutionPriceForTrade leg_id=" +
                                   std::string{leg_id});
    } catch (ValidationError const&) {
        throw;
    } catch (PersistenceError const&) {
        throw;
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"SqliteTradeLegBookingRepository::UpdateExecutionPriceForTrade: "} +
                               e.what());
    }
}

void SqliteTradeLegBookingRepository::UpdateExecutionPrices(
        const std::span<const TradeLegBookingUpdate> updates) const {
    if (updates.empty()) {
        throw ValidationError("UpdateExecutionPrices: updates must be non-empty");
    }
    for (const TradeLegBookingUpdate& u : updates) {
        UpdateExecutionPrice(u.leg_id, u.execution_price);
    }
}

void SqliteTradeLegBookingRepository::SetTradeBookingTimestamp(
        const std::string_view trade_id,
        const std::optional<std::string>& booking_timestamp) const {
    RequireNonEmpty(trade_id, "trade_id");

    try {
        if (booking_timestamp.has_value() && !booking_timestamp->empty()) {
            SQLite::Statement& st = *impl_->set_booking_timestamp;
            st.reset();
            st.clearBindings();
            st.bind(1, *booking_timestamp);
            st.bind(2, std::string{trade_id});
            st.exec();
            RequireRowsChanged(st.getChanges(), "SqliteTradeLegBookingRepository::SetTradeBookingTimestamp");
            return;
        }

        SQLite::Statement& st_now = *impl_->set_booking_timestamp_now;
        st_now.reset();
        st_now.clearBindings();
        st_now.bind(1, std::string{trade_id});
        st_now.exec();
        RequireRowsChanged(st_now.getChanges(), "SqliteTradeLegBookingRepository::SetTradeBookingTimestamp (now)");
    } catch (ValidationError const&) {
        throw;
    } catch (PersistenceError const&) {
        throw;
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"SqliteTradeLegBookingRepository::SetTradeBookingTimestamp: "} + e.what());
    }
}

void SqliteTradeLegBookingRepository::SetTradeStatus(const std::string_view trade_id,
                                                     const std::string_view status) const {
    RequireNonEmpty(trade_id, "trade_id");
    RequireNonEmpty(status, "status");

    try {
        SQLite::Statement& st = *impl_->set_trade_status;
        st.reset();
        st.clearBindings();
        st.bind(1, std::string{status});
        st.bind(2, std::string{trade_id});
        st.exec();
        RequireRowsChanged(st.getChanges(), "SqliteTradeLegBookingRepository::SetTradeStatus");
    } catch (ValidationError const&) {
        throw;
    } catch (PersistenceError const&) {
        throw;
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"SqliteTradeLegBookingRepository::SetTradeStatus: "} + e.what());
    }
}

void SqliteTradeLegBookingRepository::ApplyTradeBooking(
        const std::string_view trade_id,
        const std::span<const TradeLegBookingUpdate> leg_updates,
        const std::optional<std::string>& booking_timestamp) const {
    RequireNonEmpty(trade_id, "trade_id");
    if (leg_updates.empty()) {
        throw ValidationError("ApplyTradeBooking: leg_updates must be non-empty");
    }

    try {
        SQLite::Transaction txn(*impl_->db);
        for (const TradeLegBookingUpdate& u : leg_updates) {
            UpdateExecutionPriceForTrade(trade_id, u.leg_id, u.execution_price);
        }
        SetTradeBookingTimestamp(trade_id, booking_timestamp);
        txn.commit();
    } catch (ValidationError const&) {
        throw;
    } catch (PersistenceError const&) {
        throw;
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"SqliteTradeLegBookingRepository::ApplyTradeBooking: "} + e.what());
    }
}

}  // namespace numeraire::database
