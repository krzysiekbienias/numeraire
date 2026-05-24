#include <gtest/gtest.h>

#include <numeraire/database/leg_mtm_pnl.hpp>
#include <numeraire/database/leg_pv.hpp>
#include <numeraire/enums/position_direction.hpp>

namespace {

numeraire::database::TradeLegCatalogRow MakeLegRow(numeraire::PositionDirection direction,
                                                 double quantity,
                                                 double contract_size,
                                                 double execution_price) {
    numeraire::database::TradeLegCatalogRow row{};
    row.leg.direction = direction;
    row.leg.quantity = quantity;
    row.leg.execution_price = execution_price;
    row.equity.contract_size = contract_size;
    return row;
}

}  // namespace

TEST(LegMtmPnlTest, BookedMarkMatchesLegPvTotalWithExecutionPrice) {
    const auto row = MakeLegRow(numeraire::PositionDirection::kLong, 10.0, 100.0, 5.0);
    EXPECT_DOUBLE_EQ(numeraire::database::LegBookedMark(row),
                     numeraire::database::LegPvTotal(row.leg.direction,
                                                     row.leg.quantity,
                                                     row.equity.contract_size,
                                                     row.leg.execution_price));
    EXPECT_DOUBLE_EQ(numeraire::database::LegBookedMark(row), 5000.0);
}

TEST(LegMtmPnlTest, BookedMarkShortIsNegative) {
    const auto row = MakeLegRow(numeraire::PositionDirection::kShort, 1.0, 100.0, 4.0);
    EXPECT_DOUBLE_EQ(numeraire::database::LegBookedMark(row), -400.0);
}

TEST(LegMtmPnlTest, CommissionOrZero) {
    numeraire::database::TradeLegDto leg{};
    leg.commission = 25.0;
    EXPECT_DOUBLE_EQ(numeraire::database::LegCommissionOrZero(leg), 25.0);
    leg.commission = std::nullopt;
    EXPECT_DOUBLE_EQ(numeraire::database::LegCommissionOrZero(leg), 0.0);
}

TEST(LegMtmPnlTest, ResolvePvTotalPrevUsesPriorWhenPresent) {
    numeraire::database::PriorOfficialMtmMark prior{};
    prior.as_of = "2026-05-19";
    prior.pv_total = 600.0;
    EXPECT_DOUBLE_EQ(numeraire::database::ResolvePvTotalPrevForDaily(prior, 5000.0), 600.0);
}

TEST(LegMtmPnlTest, ResolvePvTotalPrevFallsBackToBookedMark) {
    EXPECT_DOUBLE_EQ(numeraire::database::ResolvePvTotalPrevForDaily(std::nullopt, 5000.0), 5000.0);
}
