#include <numeraire/database/sqlite_trade_repository.hpp>

#include <numeraire/utils/exception.hpp>
#include <numeraire/utils/string.hpp>

#include <SQLiteCpp/SQLiteCpp.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace numeraire::database {

struct SqliteTradeRepository::Impl {
    std::unique_ptr<SQLite::Database> db;
    std::unique_ptr<SQLite::Statement> select_catalog;
};

namespace {

constexpr const char* kSelectCatalogSql =
        "SELECT "
        "t.trade_id, t.portfolio_id, t.strategy_type, t.booking_timestamp, t.trade_date, "
        "t.updated_at, t.status, "
        "tl.leg_id, tl.product_id, tl.direction, tl.quantity, tl.execution_price, tl.commission, "
        "p.product_id, p.asset_kind, p.underlying_id, p.expiry_date, p.settlement, p.currency, "
        "p.contract_size, p.day_count, p.calendar, "
        "e.option_type, e.strike, e.instrument_type, e.exercise_style, e.structured_params "
        "FROM trades t "
        "INNER JOIN trade_legs tl ON tl.trade_id = t.trade_id "
        "INNER JOIN products p ON p.product_id = tl.product_id "
        "LEFT JOIN products_equity e ON e.product_id = tl.product_id "
        "WHERE t.trade_id = ? "
        "ORDER BY tl.leg_id";

enum class CatalogCol : int {
    kTradeId = 0,
    kPortfolioId = 1,
    kStrategyType = 2,
    kBookingTs = 3,
    kTradeDate = 4,
    kUpdatedAt = 5,
    kStatus = 6,
    kLegId = 7,
    kLegProductId = 8,
    kDirection = 9,
    kQuantity = 10,
    kExecPrice = 11,
    kCommission = 12,
    kProductId = 13,
    kAssetKind = 14,
    kUnderlyingId = 15,
    kExpiryDate = 16,
    kSettlement = 17,
    kCurrency = 18,
    kContractSize = 19,
    kDayCount = 20,
    kCalendar = 21,
    kOptionType = 22,
    kStrike = 23,
    kInstrumentType = 24,
    kExerciseStyle = 25,
    kStructuredParams = 26,
};

[[nodiscard]] std::string NormalizeEnumKey(std::string s) {
    s = utils::ToLowerAscii(utils::TrimCopy(s));
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
    const std::string k = utils::ToLowerAscii(utils::TrimCopy(raw));
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
    const std::string k = utils::ToLowerAscii(utils::TrimCopy(raw));
    if (k == "long") {
        return PositionDirection::kLong;
    }
    if (k == "short") {
        return PositionDirection::kShort;
    }
    throw PersistenceError("invalid direction in DB: " + raw);
}

[[nodiscard]] TradeHeaderDto ReadTradeHeader(SQLite::Statement const& st) {
    TradeHeaderDto h{};
    h.trade_id = ColumnText(st, static_cast<int>(CatalogCol::kTradeId));
    h.portfolio_id = ColumnText(st, static_cast<int>(CatalogCol::kPortfolioId));
    h.strategy_type = ColumnText(st, static_cast<int>(CatalogCol::kStrategyType));
    h.booking_timestamp = ColumnText(st, static_cast<int>(CatalogCol::kBookingTs));
    h.trade_date = ColumnText(st, static_cast<int>(CatalogCol::kTradeDate));
    h.updated_at = ColumnText(st, static_cast<int>(CatalogCol::kUpdatedAt));
    h.status = ColumnText(st, static_cast<int>(CatalogCol::kStatus));
    return h;
}

void AssertSameTradeHeader(SQLite::Statement const& st, TradeHeaderDto const& expected) {
    const TradeHeaderDto h = ReadTradeHeader(st);
    if (h.trade_id != expected.trade_id || h.portfolio_id != expected.portfolio_id ||
        h.strategy_type != expected.strategy_type || h.booking_timestamp != expected.booking_timestamp ||
        h.trade_date != expected.trade_date || h.updated_at != expected.updated_at ||
        h.status != expected.status) {
        throw PersistenceError("SqliteTradeRepository: inconsistent trade header across legs for trade_id: " +
                               expected.trade_id);
    }
}

[[nodiscard]] TradeLegCatalogRow MapLegCatalogRow(SQLite::Statement const& st) {
    TradeLegCatalogRow row{};

    row.leg.leg_id = ColumnText(st, static_cast<int>(CatalogCol::kLegId));
    row.leg.trade_id = ColumnText(st, static_cast<int>(CatalogCol::kTradeId));
    row.leg.product_id = ColumnText(st, static_cast<int>(CatalogCol::kLegProductId));
    row.leg.direction = ParseDirection(ColumnText(st, static_cast<int>(CatalogCol::kDirection)));
    row.leg.quantity = st.getColumn(static_cast<int>(CatalogCol::kQuantity)).getDouble();
    row.leg.execution_price =
            st.getColumn(static_cast<int>(CatalogCol::kExecPrice)).getDouble();
    if (ColumnIsNull(st, static_cast<int>(CatalogCol::kCommission))) {
        row.leg.commission = std::nullopt;
    } else {
        row.leg.commission = st.getColumn(static_cast<int>(CatalogCol::kCommission)).getDouble();
    }

    const std::string pid = ColumnText(st, static_cast<int>(CatalogCol::kProductId));
    row.product.product_id = pid;
    if (ColumnIsNull(st, static_cast<int>(CatalogCol::kOptionType))) {
        row.product.option_side = std::nullopt;
    } else {
        row.product.option_side = ColumnText(st, static_cast<int>(CatalogCol::kOptionType));
    }
    if (ColumnIsNull(st, static_cast<int>(CatalogCol::kStrike))) {
        row.product.strike = std::nullopt;
    } else {
        row.product.strike = st.getColumn(static_cast<int>(CatalogCol::kStrike)).getDouble();
    }

    if (ColumnIsNull(st, static_cast<int>(CatalogCol::kInstrumentType))) {
        row.product.catalog_instrument_type = std::nullopt;
    } else {
        const std::string ti = utils::TrimCopy(ColumnText(st, static_cast<int>(CatalogCol::kInstrumentType)));
        row.product.catalog_instrument_type =
                ti.empty() ? std::nullopt : std::optional<std::string>(ti);
    }

    if (ColumnIsNull(st, static_cast<int>(CatalogCol::kExerciseStyle))) {
        row.product.catalog_exercise_style = std::nullopt;
    } else {
        const std::string xe = utils::TrimCopy(ColumnText(st, static_cast<int>(CatalogCol::kExerciseStyle)));
        row.product.catalog_exercise_style =
                xe.empty() ? std::nullopt : std::optional<std::string>(xe);
    }

    if (ColumnIsNull(st, static_cast<int>(CatalogCol::kStructuredParams))) {
        row.product.attributes_json = "{}";
    } else {
        row.product.attributes_json = ColumnText(st, static_cast<int>(CatalogCol::kStructuredParams));
        if (row.product.attributes_json.empty()) {
            row.product.attributes_json = "{}";
        }
    }

    row.equity.product_id = pid;
    row.equity.asset_kind = ColumnText(st, static_cast<int>(CatalogCol::kAssetKind));
    row.equity.underlying_id = ColumnText(st, static_cast<int>(CatalogCol::kUnderlyingId));
    row.equity.currency = utils::TrimCopy(ColumnText(st, static_cast<int>(CatalogCol::kCurrency)));
    if (row.equity.currency.empty()) {
        row.equity.currency = "USD";
    }
    row.equity.contract_size =
            st.getColumn(static_cast<int>(CatalogCol::kContractSize)).getDouble();

    if (ColumnIsNull(st, static_cast<int>(CatalogCol::kExpiryDate))) {
        row.equity.expiry_date = std::nullopt;
    } else {
        const std::string ex = ColumnText(st, static_cast<int>(CatalogCol::kExpiryDate));
        row.equity.expiry_date = ex.empty() ? std::nullopt : std::optional<std::string>(ex);
    }

    const std::string settlement_txt = ColumnText(st, static_cast<int>(CatalogCol::kSettlement));
    const std::string day_count_txt = ColumnText(st, static_cast<int>(CatalogCol::kDayCount));
    const std::string calendar_txt = ColumnText(st, static_cast<int>(CatalogCol::kCalendar));
    if (settlement_txt.empty()) {
        row.equity.settlement = std::nullopt;
    } else {
        row.equity.settlement = ParseSettlement(settlement_txt);
    }
    if (day_count_txt.empty()) {
        row.equity.day_count = std::nullopt;
    } else {
        row.equity.day_count = ParseDayCount(day_count_txt);
    }
    if (calendar_txt.empty()) {
        row.equity.calendar = std::nullopt;
    } else {
        row.equity.calendar = ParseCalendar(calendar_txt);
    }

    return row;
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

std::vector<std::string> SqliteTradeRepository::ListAllTradeIds() const {
    try {
        SQLite::Statement st(*impl_->db, "SELECT trade_id FROM trades ORDER BY trade_id");
        std::vector<std::string> out;
        while (st.executeStep()) {
            out.push_back(st.getColumn(0).getString());
        }
        return out;
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"SqliteTradeRepository::ListAllTradeIds: "} + e.what());
    }
}

TradeCatalogBundle SqliteTradeRepository::GetCatalogForTrade(const std::string_view trade_id) const {
    try {
        SQLite::Statement& st = *impl_->select_catalog;
        st.reset();
        st.clearBindings();
        st.bind(1, std::string(trade_id));

        if (!st.executeStep()) {
            throw PersistenceError("SqliteTradeRepository: trade not found or no legs: " +
                                   std::string(trade_id));
        }

        TradeCatalogBundle bundle{};
        bundle.trade = ReadTradeHeader(st);
        std::unordered_set<std::string> seen_legs;

        TradeLegCatalogRow first = MapLegCatalogRow(st);
        if (!seen_legs.insert(first.leg.leg_id).second) {
            throw PersistenceError("SqliteTradeRepository: duplicate leg_id in database: " +
                                   first.leg.leg_id);
        }
        bundle.legs.push_back(std::move(first));

        while (st.executeStep()) {
            AssertSameTradeHeader(st, bundle.trade);
            TradeLegCatalogRow row = MapLegCatalogRow(st);
            if (!seen_legs.insert(row.leg.leg_id).second) {
                throw PersistenceError("SqliteTradeRepository: duplicate leg_id in database: " +
                                       row.leg.leg_id);
            }
            bundle.legs.push_back(std::move(row));
        }

        return bundle;
    } catch (PersistenceError const&) {
        throw;
    } catch (SQLite::Exception const& e) {
        throw PersistenceError(std::string{"SqliteTradeRepository: "} + e.what());
    }
}

}  // namespace numeraire::database
