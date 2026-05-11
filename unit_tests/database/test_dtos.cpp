#include <gtest/gtest.h>

#include <numeraire/database/dtos.hpp>

TEST(DatabaseDtosTest, ProductDtoDefaultAggregates) {
    const numeraire::database::ProductDto p{};
    EXPECT_TRUE(p.product_id.empty());
    EXPECT_FALSE(p.option_side.has_value());
    EXPECT_FALSE(p.strike.has_value());
    EXPECT_TRUE(p.attributes_json.empty());
}

TEST(DatabaseDtosTest, TradeDtoRoundTripSampleShape) {
    numeraire::database::TradeDto t{};
    t.trade_id = "TRD_001";
    t.product_id = "P_AAPL_001";
    t.booking_timestamp = "2025-08-04 10:00:00";
    t.trade_date = "2025-08-06";
    t.updated_at = "2026-05-11 16:34:30";
    t.status = "LIVE";
    t.direction = numeraire::PositionDirection::kLong;
    t.quantity = 100.0;
    t.commission = 0.0;

    EXPECT_EQ(t.trade_id, "TRD_001");
    EXPECT_EQ(t.product_id, "P_AAPL_001");
    EXPECT_EQ(t.direction, numeraire::PositionDirection::kLong);
    ASSERT_TRUE(t.commission.has_value());
    EXPECT_DOUBLE_EQ(*t.commission, 0.0);
}
