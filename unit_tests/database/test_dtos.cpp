#include <gtest/gtest.h>

#include <numeraire/database/dtos.hpp>
#include <numeraire/enums/position_direction.hpp>

TEST(DatabaseDtosTest, ProductDtoDefaultAggregates) {
    const numeraire::database::ProductDto p{};
    EXPECT_TRUE(p.product_id.empty());
    EXPECT_FALSE(p.option_side.has_value());
    EXPECT_FALSE(p.catalog_instrument_type.has_value());
    EXPECT_FALSE(p.catalog_exercise_style.has_value());
}

TEST(DatabaseDtosTest, TradeHeaderDtoSampleShape) {
    numeraire::database::TradeHeaderDto h{};
    h.trade_id = "TRD_001";
    h.portfolio_id = "BOOK_1";
    h.strategy_type = "VANILLA_OPTION";
    h.booking_timestamp = "2025-08-04 10:00:00";
    h.trade_date = "2025-08-06";
    h.updated_at = "2026-05-11 16:34:30";
    h.status = "LIVE";

    EXPECT_EQ(h.trade_id, "TRD_001");
    EXPECT_EQ(h.portfolio_id, "BOOK_1");
}

TEST(DatabaseDtosTest, TradeLegDtoSampleShape) {
    numeraire::database::TradeLegDto leg{};
    leg.leg_id = "TRD_001_L1";
    leg.trade_id = "TRD_001";
    leg.product_id = "P_AAPL_001";
    leg.direction = numeraire::PositionDirection::kLong;
    leg.quantity = 100.0;
    leg.execution_price = 12.5;
    leg.commission = 0.0;

    EXPECT_EQ(leg.leg_id, "TRD_001_L1");
    ASSERT_TRUE(leg.commission.has_value());
    EXPECT_DOUBLE_EQ(*leg.commission, 0.0);
}
