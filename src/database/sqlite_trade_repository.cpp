#include <numeraire/database/sqlite_trade_repository.hpp>

#include <numeraire/utils/exception.hpp>

#include <SQLiteCpp/SQLiteCpp.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>

namespace numeraire::database {

struct SqliteTradeRepository::Impl {
    std::unique_ptr<SQLite::Database> db;
    std::unique_ptr<SQLite::Statement> select_catalog;
};

namespace {

constexpr const char* kSelectCatalogSql =
        "SELECT "
        "t.trade_id, t.product_id, t.booking_timestamp, t.trade_date, t.updated_at, "
        "t.status, t.direction, t.quantity, t.commission, "
        "p.product_id, p.asset_kind, p.underlying_id, p.expiry_date, p.settlement, p.day_count, p.calendar, "
        "e.option_type, e.strike, e.structured_params "
        "FROM trades t "
        "INNER JOIN products p ON p.product_id = t.product_id "
        "INNER JOIN products_equity e ON e.product_id = t.product_id "
        "WHERE t.trade_id = ?";

enum class CatalogCol : int {
    kTradeId = 0,
    kTradeProductId = 1,
    kBookingTs = 2,
    kTradeDate = 3,
    kUpdatedAt = 4,
    kStatus = 5,
    kDirection = 6,
    kQuantity = 7,
    kCommission = 8,
    kProductId = 9,
    kAssetKind = 10,
    kUnderlyingId = 11,
    kExpiryDate = 12,
    kSettlement = 13,
    kDayCount = 14,
    kCalendar = 15,
    kOptionType = 16,
    kStrike = 17,
    kStructuredParams = 18,
};

[[nodiscard]] std::string TrimCopy(std::string s) {
    const auto not_space = [](const unsigned char ch) { return std::isspace(ch) == 0; };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

[[nodiscard]] std::string ToLowerAscii(std::string s) {
    for (char& ch : s) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return s;
}

[[nodiscard]] std::string NormalizeEnumKey(std::string s) {
    s = ToLowerAscii(TrimCopy(std::move(s)));
    s.erase(std::remove_if(s.begin(), s.end(),
                           [](const unsigned char c) {
                               return c == '/' || c == ' ' || c == '-' || c == '_';
                           }),
            s.end());
    return s;
}

[[nodiscard]] std::string ColumnText(SQLite::Statement const& st, const int i) {
    SQLite::Column col = st.getColumn(i);
    if (col.isNull()) {
        return {};
    }
    return col.getString();
}

[[nodiscard]] bool ColumnIsNull(SQLite::Statement const& st, const int i) {
    return st.getColumn(i).isNull();
}

[[nodiscard]] SettlementType ParseSettlement(const std::string& raw) {
    const std::string k = ToLowerAscii(TrimCopy(raw));
    if (k == "cash") {
        return SettlementType::kCash;
    }
    if (k == "physical") {
        return SettlementType::kPhysical;
    }
    throw PersistenceError("invalid settlement in DB: " + raw);
}

[[nodiscard]] DayCount ParseDayCount(const std::string& raw) {
    const std::string k = NormalizeEnumKey(raw);
    if (k == "actual360" || k == "act360") {
        return DayCount::kActual360;
    }
    if (k == "actual365fixed" || k == "act365fixed") {
        return DayCount::kActual365Fixed;
    }
    if (k == "thirty360" || k == "30360") {
        return DayCount::kThirty360;
    }
    if (k == "actualactualisda" || k == "actactisda") {
        return DayCount::kActualActualIsda;
    }
    if (k == "actualactualbond" || k == "actactbond") {
        return DayCount::kActualActualBond;
    }
    if (k == "business252" || k == "bus252") {
        return DayCount::kBusiness252;
    }
    throw PersistenceError("invalid day_count in DB: " + raw);
}

[[nodiscard]] CalendarType ParseCalendar(const std::string& raw) {
    const std::string k = NormalizeEnumKey(raw);
    if (k == "unitedstates" || k == "us") {
        return CalendarType::kUnitedStates;
    }
    if (k == "unitedkingdom" || k == "uk") {
        return CalendarType::kUnitedKingdom;
    }
    if (k == "target") {
        return CalendarType::kTarget;
    }
    if (k == "germany") {
        return CalendarType::kGermany;
    }
    throw PersistenceError("invalid calendar in DB: " + raw);
}

[[nodiscard]] PositionDirection ParseDirection(const std::string& raw) {
    const std::string k = ToLowerAscii(TrimCopy(raw));
    if (k == "long") {
        return PositionDirection::kLong;
    }
    if (k == "short") {
        return PositionDirection::kShort;
    }
    throw PersistenceError("invalid direction in DB: " + raw);
}

[[nodiscard]] TradeCatalogBundle MapRowToBundle(SQLite::Statement const& st) {
    TradeCatalogBundle b{};
    b.trade.trade_id = ColumnText(st, static_cast<int>(CatalogCol::kTradeId));
    b.trade.product_id = ColumnText(st, static_cast<int>(CatalogCol::kTradeProductId));
    b.trade.booking_timestamp = ColumnText(st, static_cast<int>(CatalogCol::kBookingTs));
    b.trade.trade_date = ColumnText(st, static_cast<int>(CatalogCol::kTradeDate));
    b.trade.updated_at = ColumnText(st, static_cast<int>(CatalogCol::kUpdatedAt));
    b.trade.status = ColumnText(st, static_cast<int>(CatalogCol::kStatus));
    b.trade.direction = ParseDirection(ColumnText(st, static_cast<int>(CatalogCol::kDirection)));
    b.trade.quantity = st.getColumn(static_cast<int>(CatalogCol::kQuantity)).getDouble();

    if (ColumnIsNull(st, static_cast<int>(CatalogCol::kCommission))) {
        b.trade.commission = std::nullopt;
    } else {
        b.trade.commission = st.getColumn(static_cast<int>(CatalogCol::kCommission)).getDouble();
    }

    b.product.product_id = ColumnText(st, static_cast<int>(CatalogCol::kProductId));
    if (ColumnIsNull(st, static_cast<int>(CatalogCol::kOptionType))) {
        b.product.option_side = std::nullopt;
    } else {
        b.product.option_side = ColumnText(st, static_cast<int>(CatalogCol::kOptionType));
    }
    if (ColumnIsNull(st, static_cast<int>(CatalogCol::kStrike))) {
        b.product.strike = std::nullopt;
    } else {
        b.product.strike = st.getColumn(static_cast<int>(CatalogCol::kStrike)).getDouble();
    }
    if (ColumnIsNull(st, static_cast<int>(CatalogCol::kStructuredParams))) {
        b.product.attributes_json = "{}";
    } else {
        b.product.attributes_json = ColumnText(st, static_cast<int>(CatalogCol::kStructuredParams));
        if (b.product.attributes_json.empty()) {
            b.product.attributes_json = "{}";
        }
    }

    b.equity.product_id = ColumnText(st, static_cast<int>(CatalogCol::kProductId));
    b.equity.asset_kind = ColumnText(st, static_cast<int>(CatalogCol::kAssetKind));
    b.equity.underlying_id = ColumnText(st, static_cast<int>(CatalogCol::kUnderlyingId));
    b.equity.expiry_date = ColumnText(st, static_cast<int>(CatalogCol::kExpiryDate));

    const std::string settlement_txt = ColumnText(st, static_cast<int>(CatalogCol::kSettlement));
    const std::string day_count_txt = ColumnText(st, static_cast<int>(CatalogCol::kDayCount));
    const std::string calendar_txt = ColumnText(st, static_cast<int>(CatalogCol::kCalendar));
    if (settlement_txt.empty()) {
        b.equity.settlement = std::nullopt;
    } else {
        b.equity.settlement = ParseSettlement(settlement_txt);
    }
    if (day_count_txt.empty()) {
        b.equity.day_count = std::nullopt;
    } else {
        b.equity.day_count = ParseDayCount(day_count_txt);
    }
    if (calendar_txt.empty()) {
        b.equity.calendar = std::nullopt;
    } else {
        b.equity.calendar = ParseCalendar(calendar_txt);
    }

    return b;
}

}  // namespace

SqliteTradeRepository::SqliteTradeRepository(std::string database_file_path)
        : impl_(std::make_unique<Impl>()) {
    try {
        impl_->db = std::make_unique<SQLite::Database>(
                database_file_path, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        impl_->db->exec("PRAGMA foreign_keys = ON;");
        impl_->select_catalog =
                std::make_unique<SQLite::Statement>(*impl_->db, kSelectCatalogSql);
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"SqliteTradeRepository: "} + e.what());
    }
}

SqliteTradeRepository::~SqliteTradeRepository() = default;

TradeCatalogBundle SqliteTradeRepository::GetCatalogForTrade(const std::string_view trade_id) {
    try {
        SQLite::Statement& st = *impl_->select_catalog;
        st.reset();
        st.clearBindings();
        st.bind(1, std::string(trade_id));

        if (!st.executeStep()) {
            throw PersistenceError("SqliteTradeRepository: trade not found or catalog join incomplete: " +
                                   std::string(trade_id));
        }

        TradeCatalogBundle bundle = MapRowToBundle(st);

        if (st.executeStep()) {
            throw PersistenceError("SqliteTradeRepository: duplicate trade_id in database: " +
                                   std::string(trade_id));
        }

        return bundle;
    } catch (PersistenceError const&) {
        throw;
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"SqliteTradeRepository: "} + e.what());
    }
}

}  // namespace numeraire::database
