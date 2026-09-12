#include <numeraire/database/trade_booking_rules.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/utils/exception.hpp>
#include <numeraire/utils/string.hpp>

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace numeraire::database {

namespace {

[[nodiscard]] std::string NormalizeStatusKey(std::string_view status) {
    std::string out(status);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::toupper(c); });
    return out;
}

[[nodiscard]] std::string NormalizeInstrumentTypeKey(std::string t) {
    t = numeraire::utils::ToLowerAscii(numeraire::utils::TrimCopy(t));
    t.erase(std::remove_if(t.begin(), t.end(),
                           [](const unsigned char c) { return c == '_' || std::isspace(c) != 0; }),
            t.end());
    return t;
}

[[nodiscard]] bool IsLinearForwardInstrumentType(std::string_view raw) {
    const std::string key = NormalizeInstrumentTypeKey(std::string{raw});
    return key == "equityforward" || key == "forward" || key == "commodityfuturesforward" ||
           key == "futuresforward";
}

[[nodiscard]] std::string LegInstrumentType(const TradeLegCatalogRow& row) {
    if (row.product.catalog_instrument_type.has_value() &&
        !row.product.catalog_instrument_type->empty()) {
        return *row.product.catalog_instrument_type;
    }
    if (row.commodity.has_value()) {
        return row.commodity->instrument_type;
    }
    return {};
}

[[nodiscard]] bool LegIsBooked(const TradeLegCatalogRow& row) {
    if (row.leg.execution_price > 0.0) {
        return true;
    }
    if (row.leg.execution_price < 0.0) {
        return false;
    }
    return IsLinearForwardInstrumentType(LegInstrumentType(row));
}

}  // namespace

bool TradeStatusEquals(const std::string_view actual, const std::string_view expected) noexcept {
    return NormalizeStatusKey(actual) == NormalizeStatusKey(expected);
}

std::string RequireTradeDateIso(const TradeHeaderDto& trade) {
    const std::string trimmed = numeraire::utils::TrimCopy(trade.trade_date);
    if (trimmed.empty()) {
        throw ValidationError("trade " + trade.trade_id + ": trade_date is required for booking (YYYY-MM-DD)");
    }
    (void)schedule::ParseIsoDate(trimmed);
    return trimmed;
}

void RequireValuationDateEqualsTradeDate(const schedule::Date& valuation_date, const TradeHeaderDto& trade) {
    const std::string trade_date_iso = RequireTradeDateIso(trade);
    const schedule::Date trade_date = schedule::ParseIsoDate(trade_date_iso);
    if (valuation_date.year != trade_date.year || valuation_date.month != trade_date.month ||
        valuation_date.day != trade_date.day) {
        throw ValidationError(
                "trade " + trade.trade_id + ": ValuationDate must equal trade_date for booking (valuation " +
                std::to_string(valuation_date.year) + "-" + std::to_string(valuation_date.month) + "-" +
                std::to_string(valuation_date.day) + " vs trade_date " + trade_date_iso + ")");
    }
}

void RequireTradePendingForBooking(const TradeHeaderDto& trade) {
    if (!TradeStatusEquals(trade.status, kTradeStatusPending)) {
        throw ValidationError("trade " + trade.trade_id + ": booking requires status PENDING (got: " + trade.status +
                              ")");
    }
}

void RequireTradeLiveForMtm(const TradeHeaderDto& trade) {
    if (!TradeStatusEquals(trade.status, kTradeStatusLive)) {
        throw ValidationError("trade " + trade.trade_id + ": MTM requires status LIVE (got: " + trade.status +
                              "); run --price-booking first.");
    }
}

void RequireAllLegsBookedForMtm(const TradeCatalogBundle& bundle) {
    for (const TradeLegCatalogRow& row : bundle.legs) {
        if (!LegIsBooked(row)) {
            throw ValidationError("trade " + bundle.trade.trade_id + ": leg " + row.leg.leg_id +
                                  " has no booked execution_price (must be > 0 before MTM, "
                                  "or 0 for an ATM linear forward)");
        }
    }
}

void RequireMtmAsOfNotBeforeTradeDate(const std::string_view as_of_iso, const TradeHeaderDto& trade) {
    const std::string trade_date_iso = RequireTradeDateIso(trade);
    if (as_of_iso < trade_date_iso) {
        throw ValidationError("trade " + trade.trade_id + ": MTM as_of " + std::string{as_of_iso} +
                              " must not be before trade_date " + trade_date_iso);
    }
}

bool AllLegsBooked(const TradeCatalogBundle& bundle) {
    if (bundle.legs.empty()) {
        return false;
    }
    for (const TradeLegCatalogRow& row : bundle.legs) {
        if (!LegIsBooked(row)) {
            return false;
        }
    }
    return true;
}

bool AllLegExecutionPricesPositive(const TradeCatalogBundle& bundle) {
    for (const TradeLegCatalogRow& row : bundle.legs) {
        if (row.leg.execution_price <= 0.0) {
            return false;
        }
    }
    return !bundle.legs.empty();
}

}  // namespace numeraire::database
