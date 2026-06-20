#include <gtest/gtest.h>

#include <numeraire/enums/option_type.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/simulation/exposure_time_grid.hpp>
#include <numeraire/simulation/path_pricer.hpp>
#include <numeraire/simulation/path_pricing_quotes.hpp>
#include <numeraire/simulation/scenario_buffer.hpp>
#include <numeraire/simulation/scenario_slice_market_data.hpp>

#include <string>
#include <vector>

namespace {

using numeraire::OptionType;
using numeraire::schedule::ParseIsoDate;
using numeraire::simulation::BuildFactorIndexByUnderlying;
using numeraire::simulation::ExposureGridNode;
using numeraire::simulation::ExposureTimeGrid;
using numeraire::simulation::PathPricingQuotes;
using numeraire::simulation::ScenarioBuffer;
using numeraire::simulation::ScenarioSliceMarketData;

ExposureTimeGrid SimpleGrid() {
    ExposureTimeGrid grid;
    grid.valuation_date = ParseIsoDate("2026-06-15");
    grid.nodes = {
            ExposureGridNode{.date = ParseIsoDate("2026-06-15"), .year_fraction = 0.0, .target_dte_days = 0, .pillar_id = "ASOF"},
            ExposureGridNode{.date = ParseIsoDate("2026-07-15"), .year_fraction = 0.082191780821917804, .target_dte_days = 30, .pillar_id = "M1"},
    };
    return grid;
}

}  // namespace

TEST(ScenarioSliceMarketDataTest, SpotMatchesScenarioBufferSlice) {
    const auto grid = SimpleGrid();
    ScenarioBuffer buffer(1, grid.NumSteps(), 2);
    buffer.At(0, 0, 0) = 100.0;
    buffer.At(0, 0, 1) = 101.0;
    buffer.At(0, 1, 0) = 110.0;
    buffer.At(0, 1, 1) = 111.0;

    const std::vector<std::string> factors{"AAPL"};
    const auto factor_map = BuildFactorIndexByUnderlying(factors);
    const PathPricingQuotes quotes{.risk_free_rate = 0.03, .dividend_yield = 0.0, .flat_implied_volatility = 0.2};

    ScenarioSliceMarketData market(buffer, grid, factor_map, quotes);
    market.SetSlice(0, 1);
    EXPECT_DOUBLE_EQ(market.Spot("AAPL"), 101.0);
    EXPECT_EQ(market.ValuationDate().year, 2026);
    EXPECT_EQ(market.ValuationDate().month, 6);
    EXPECT_EQ(market.ValuationDate().day, 15);

    market.SetSlice(1, 0);
    EXPECT_DOUBLE_EQ(market.Spot("AAPL"), 110.0);
    EXPECT_DOUBLE_EQ(market.RiskFreeRate(), 0.03);
    EXPECT_DOUBLE_EQ(market.ImpliedVolatility("AAPL", 100.0, 0.5, OptionType::kCall), 0.2);
}
