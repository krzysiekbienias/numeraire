#include <gtest/gtest.h>

#include <numeraire/enums/exercise_style.hpp>
#include <numeraire/enums/model_type.hpp>
#include <numeraire/enums/option_type.hpp>
#include <numeraire/enums/position_direction.hpp>
#include <numeraire/enums/pricing_engine_type.hpp>
#include <numeraire/pricers/pricer_factory.hpp>
#include <numeraire/products/commodity_futures_outright_product.hpp>
#include <numeraire/products/vanilla_equity_option_product.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/simulation/exposure_time_grid.hpp>
#include <numeraire/simulation/path_pricer.hpp>
#include <numeraire/simulation/path_pricing_market_config.hpp>
#include <numeraire/simulation/scenario_buffer.hpp>

#include <memory>
#include <string>
#include <vector>

namespace {

using numeraire::ExerciseStyle;
using numeraire::ModelType;
using numeraire::OptionType;
using numeraire::PositionDirection;
using numeraire::PricingEngineType;
using numeraire::schedule::ParseIsoDate;
using numeraire::simulation::ExposureGridNode;
using numeraire::simulation::ExposureTimeGrid;
using numeraire::simulation::LegPathPvBuffer;
using numeraire::simulation::PathPricingLegEntry;
using numeraire::simulation::PathPricingMarketConfig;
using numeraire::simulation::PathPricingQuotes;
using numeraire::simulation::PricePortfolioAlongPaths;
using numeraire::simulation::ScenarioBuffer;

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

TEST(PathPricerTest, WritesPvForEachLegStepPath) {
    const auto grid = SimpleGrid();
    ScenarioBuffer buffer(1, grid.NumSteps(), 2);
    for (std::size_t step = 0; step < grid.NumSteps(); ++step) {
        for (std::size_t path = 0; path < buffer.NumPaths(); ++path) {
            buffer.At(0, step, path) = 100.0 + static_cast<double>(path) + static_cast<double>(step);
        }
    }

    auto product = std::make_unique<numeraire::products::VanillaEquityOptionProduct>(
            "AAPL", OptionType::kCall, ExerciseStyle::kEuropean, 100.0, ParseIsoDate("2026-01-01"),
            ParseIsoDate("2027-01-01"));

    PathPricingLegEntry leg;
    leg.leg_id = "LEG_1";
    leg.trade_id = "TRD_1";
    leg.underlying_id = "AAPL";
    leg.factor_index = 0;
    leg.direction = PositionDirection::kLong;
    leg.quantity = 1.0;
    leg.contract_size = 100.0;
    leg.expiry_date = ParseIsoDate("2027-01-01");
    leg.product = std::move(product);

    std::vector<PathPricingLegEntry> legs;
    legs.push_back(std::move(leg));

    const std::vector<std::string> factors{"AAPL"};
    const PathPricingQuotes flat_fallbacks{
            .risk_free_rate = 0.03, .dividend_yield = 0.0, .flat_implied_volatility = 0.2};
    PathPricingMarketConfig market_config{};
    market_config.flat_fallbacks = flat_fallbacks;
    auto pricer = numeraire::pricers::PricerFactory::Make(PricingEngineType::kAnalytic, ModelType::kBlackScholes);

    LegPathPvBuffer out_pv(1, grid.NumSteps(), buffer.NumPaths());
    std::vector<std::string> out_leg_ids;
    PricePortfolioAlongPaths(buffer, grid, factors, legs, market_config, *pricer, out_pv, out_leg_ids);

    ASSERT_EQ(out_leg_ids.size(), 1U);
    EXPECT_EQ(out_leg_ids.front(), "LEG_1");
    EXPECT_GT(out_pv.At(0, 0, 0), 0.0);
    EXPECT_GT(out_pv.At(0, 1, 1), out_pv.At(0, 0, 0));
}

/// Outright pricer returns F − K. The booked mark (`execution_price`) is settle-on-trade-date − K,
/// not a fill to subtract again. Path PV per unit must be F_path − K even when that mark is non-zero.
TEST(PathPricerTest, FuturesOutrightPathPvIgnoresBookedExecutionPrice) {
    ExposureTimeGrid grid;
    grid.valuation_date = ParseIsoDate("2026-06-15");
    grid.nodes = {ExposureGridNode{.date = ParseIsoDate("2026-06-15"),
                                   .year_fraction = 0.0,
                                   .target_dte_days = 0,
                                   .pillar_id = "ASOF"}};

    ScenarioBuffer buffer(1, grid.NumSteps(), 1);
    buffer.At(0, 0, 0) = 82.5;

    auto product = std::make_unique<numeraire::products::CommodityFuturesOutrightProduct>(
            "CLX6", "CL", ParseIsoDate("2026-06-01"), ParseIsoDate("2026-10-20"), /*delivery_price=*/80.0);

    PathPricingLegEntry leg;
    leg.leg_id = "L_FUT";
    leg.trade_id = "TRD_FUT";
    leg.underlying_id = "CLX6";
    leg.factor_index = 0;
    leg.direction = PositionDirection::kLong;
    leg.quantity = 1.0;
    leg.contract_size = 1.0;
    leg.expiry_date = ParseIsoDate("2026-10-20");
    leg.product = std::move(product);
    // Booked mark execution_price=0.5 is not applied: path PV is F − K only.

    std::vector<PathPricingLegEntry> legs;
    legs.push_back(std::move(leg));

    const std::vector<std::string> factors{"CLX6"};
    PathPricingMarketConfig market_config{};
    market_config.flat_fallbacks = PathPricingQuotes{
            .risk_free_rate = 0.03, .dividend_yield = 0.0, .flat_implied_volatility = 0.2};
    auto pricer = numeraire::pricers::PricerFactory::Make(PricingEngineType::kAnalytic, ModelType::kBlackScholes);

    LegPathPvBuffer out_pv(1, grid.NumSteps(), buffer.NumPaths());
    std::vector<std::string> out_leg_ids;
    PricePortfolioAlongPaths(buffer, grid, factors, legs, market_config, *pricer, out_pv, out_leg_ids);

    ASSERT_EQ(out_leg_ids.size(), 1U);
    EXPECT_EQ(out_leg_ids.front(), "L_FUT");
    EXPECT_DOUBLE_EQ(out_pv.At(0, 0, 0), 2.5);
}
