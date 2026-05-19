#include <gtest/gtest.h>

#include <numeraire/database/leg_pv.hpp>
#include <numeraire/enums/position_direction.hpp>
#include <numeraire/utils/exception.hpp>

TEST(LegPvTest, PositionSignLongAndShort) {
    EXPECT_DOUBLE_EQ(numeraire::database::PositionSign(numeraire::PositionDirection::kLong), 1.0);
    EXPECT_DOUBLE_EQ(numeraire::database::PositionSign(numeraire::PositionDirection::kShort), -1.0);
}

TEST(LegPvTest, LongTenContractsHundredMultiplier) {
    const double pv = numeraire::database::LegPvTotal(numeraire::PositionDirection::kLong,
                                                      10.0, 100.0, 2.0);
    EXPECT_DOUBLE_EQ(pv, 2000.0);
}

TEST(LegPvTest, ShortGooglCallRegressionFromMtmLog) {
    // OPT_PVE_GOOGL_C_400_20260918: SHORT qty=25, contract_size=100, pv_unit from BS on 2026-05-01.
    const double pv_unit = 14.8087796956056;
    const double pv = numeraire::database::LegPvTotal(numeraire::PositionDirection::kShort, 25.0, 100.0,
                                                      pv_unit);
    EXPECT_NEAR(pv, -37021.9492390141, 1e-6);
}

TEST(LegPvTest, LongUnitContractSize) {
    const double pv = numeraire::database::LegPvTotal(numeraire::PositionDirection::kLong, 2.0, 1.0, 3.0);
    EXPECT_DOUBLE_EQ(pv, 6.0);
}

TEST(LegPvTest, RequirePositiveContractSizeAcceptsTypicalEquityOption) {
    EXPECT_NO_THROW(numeraire::database::RequirePositiveContractSize(100.0, "TRD_001_L1"));
}

TEST(LegPvTest, RequirePositiveContractSizeRejectsZero) {
    EXPECT_THROW(numeraire::database::RequirePositiveContractSize(0.0, "TRD_001_L1"),
                 numeraire::ValidationError);
}

TEST(LegPvTest, RequirePositiveContractSizeRejectsNegative) {
    EXPECT_THROW(numeraire::database::RequirePositiveContractSize(-1.0, "TRD_001_L1"),
                 numeraire::ValidationError);
}

TEST(LegPvTest, RequirePositiveQuantityAcceptsTypicalBookedLeg) {
    EXPECT_NO_THROW(numeraire::database::RequirePositiveQuantity(25.0, "TRD_001_L1"));
}

TEST(LegPvTest, RequirePositiveQuantityRejectsZero) {
    EXPECT_THROW(numeraire::database::RequirePositiveQuantity(0.0, "TRD_001_L1"), numeraire::ValidationError);
}

TEST(LegPvTest, RequirePositiveQuantityRejectsNegative) {
    EXPECT_THROW(numeraire::database::RequirePositiveQuantity(-5.0, "TRD_001_L1"), numeraire::ValidationError);
}
