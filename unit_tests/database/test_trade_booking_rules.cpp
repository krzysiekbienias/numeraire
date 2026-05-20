#include <gtest/gtest.h>

#include <numeraire/database/trade_booking_rules.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/utils/exception.hpp>

namespace {

numeraire::database::TradeCatalogBundle MakeBundle(const std::string& status, const double execution_price) {
    numeraire::database::TradeCatalogBundle b;
    b.trade.trade_id = "TRD_T";
    b.trade.trade_date = "2026-05-01";
    b.trade.status = status;
    numeraire::database::TradeLegCatalogRow leg;
    leg.leg.leg_id = "TRD_T_L1";
    leg.leg.execution_price = execution_price;
    b.legs.push_back(leg);
    return b;
}

}  // namespace

TEST(TradeBookingRulesTest, RequireTradeDateIsoRejectsEmpty) {
    auto b = MakeBundle("PENDING", 0.0);
    b.trade.trade_date.clear();
    EXPECT_THROW(numeraire::database::RequireTradeDateIso(b.trade), numeraire::ValidationError);
}

TEST(TradeBookingRulesTest, ValuationDateMustMatchTradeDate) {
    auto b = MakeBundle("PENDING", 0.0);
    const numeraire::schedule::Date valuation{2026, 5, 2};
    EXPECT_THROW(numeraire::database::RequireValuationDateEqualsTradeDate(valuation, b.trade),
                 numeraire::ValidationError);
    const numeraire::schedule::Date ok{2026, 5, 1};
    EXPECT_NO_THROW(numeraire::database::RequireValuationDateEqualsTradeDate(ok, b.trade));
}

TEST(TradeBookingRulesTest, BookingRequiresPendingMtmRequiresLive) {
    auto pending = MakeBundle("PENDING", 0.0);
    auto live = MakeBundle("LIVE", 12.0);
    EXPECT_NO_THROW(numeraire::database::RequireTradePendingForBooking(pending.trade));
    EXPECT_THROW(numeraire::database::RequireTradePendingForBooking(live.trade), numeraire::ValidationError);
    EXPECT_NO_THROW(numeraire::database::RequireTradeLiveForMtm(live.trade));
    EXPECT_THROW(numeraire::database::RequireTradeLiveForMtm(pending.trade), numeraire::ValidationError);
}

TEST(TradeBookingRulesTest, MtmRequiresBookedLegs) {
    auto pending = MakeBundle("LIVE", 0.0);
    EXPECT_THROW(numeraire::database::RequireAllLegsBookedForMtm(pending), numeraire::ValidationError);
    auto live = MakeBundle("LIVE", 1.0);
    EXPECT_NO_THROW(numeraire::database::RequireAllLegsBookedForMtm(live));
}

TEST(TradeBookingRulesTest, MtmAsOfNotBeforeTradeDate) {
    auto b = MakeBundle("LIVE", 1.0);
    EXPECT_NO_THROW(numeraire::database::RequireMtmAsOfNotBeforeTradeDate("2026-05-01", b.trade));
    EXPECT_NO_THROW(numeraire::database::RequireMtmAsOfNotBeforeTradeDate("2026-05-15", b.trade));
    EXPECT_THROW(numeraire::database::RequireMtmAsOfNotBeforeTradeDate("2026-04-30", b.trade),
                 numeraire::ValidationError);
}
