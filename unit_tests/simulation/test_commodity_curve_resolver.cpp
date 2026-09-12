#include <SQLiteCpp/SQLiteCpp.h>
#include <gtest/gtest.h>

#include <numeraire/schedule/date.hpp>
#include <numeraire/schedule/format_iso_date.hpp>
#include <numeraire/simulation/commodity_curve_resolver.hpp>
#include <numeraire/simulation/exposure_time_grid.hpp>
#include <numeraire/simulation/path_pricer.hpp>
#include <numeraire/simulation/path_pricing_market_config.hpp>
#include <numeraire/simulation/scenario_buffer.hpp>
#include <numeraire/simulation/scenario_slice_market_data.hpp>
#include <numeraire/utils/exception.hpp>
#include <numeraire/utils/logger.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>
#include <tuple>
#include <vector>

namespace fs = std::filesystem;

namespace {

using numeraire::schedule::ParseIsoDate;
using numeraire::simulation::BuildCommodityCurveResolver;
using numeraire::simulation::BuildFactorIndexByUnderlying;
using numeraire::simulation::CommodityContractRef;
using numeraire::simulation::CommodityCurveResolver;
using numeraire::simulation::ExposureGridNode;
using numeraire::simulation::ExposureTimeGrid;
using numeraire::simulation::PathPricingMarketConfig;
using numeraire::simulation::PathPricingQuotes;
using numeraire::simulation::ScenarioBuffer;
using numeraire::simulation::ScenarioSliceMarketData;

constexpr const char* kAsOf = "2026-09-02";

/// A CL curve whose pillars sit at 30 / 61 / 89 days out on `kAsOf`.
[[nodiscard]] fs::path SeedCurve() {
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
    const fs::path path = fs::temp_directory_path() / ("numeraire_curve_ut_" + std::to_string(ns) + ".sqlite3");

    SQLite::Database db(path.string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec(
            "CREATE TABLE futures_contract (ticker TEXT, listing_as_of TEXT, product_code TEXT, "
            "settlement_date TEXT);");
    db.exec(
            "CREATE TABLE futures_daily_eod (ticker TEXT, as_of TEXT, close REAL, settlement_price REAL);");
    db.exec(
            "INSERT INTO futures_contract (ticker, listing_as_of, product_code, settlement_date) VALUES "
            "('CLV6', '2026-01-01', 'CL', '2026-10-02'), "
            "('CLX6', '2026-01-01', 'CL', '2026-11-02'), "
            "('CLZ6', '2026-01-01', 'CL', '2026-11-30');");
    db.exec(
            "INSERT INTO futures_daily_eod (ticker, as_of, close, settlement_price) VALUES "
            "('CLV6', '2026-09-02', 90.0, 90.0), "
            "('CLX6', '2026-09-02', 88.0, 88.0), "
            "('CLZ6', '2026-09-02', 86.0, 86.0);");
    return path;
}

[[nodiscard]] ExposureTimeGrid GridOf(const std::vector<std::string>& dates) {
    ExposureTimeGrid grid;
    grid.valuation_date = ParseIsoDate(kAsOf);
    for (const std::string& date : dates) {
        grid.nodes.push_back(ExposureGridNode{.date = ParseIsoDate(date),
                                              .year_fraction = 0.0,
                                              .target_dte_days = 0,
                                              .pillar_id = date});
    }
    return grid;
}

const std::vector<std::string> kFactors{"CL_M1", "CL_M2", "CL_M3"};

/// Resolved level of `ticker` at every grid node, with all pillars pinned to their
/// as-of settles so only the curve mapping moves.
[[nodiscard]] std::vector<double> ResolvedLevels(const CommodityCurveResolver& resolver,
                                                 const ExposureTimeGrid& grid,
                                                 const std::string& ticker) {
    ScenarioBuffer buffer(3, grid.NumSteps(), 1);
    for (std::size_t step = 0; step < grid.NumSteps(); ++step) {
        buffer.At(0, step, 0) = 90.0;
        buffer.At(1, step, 0) = 88.0;
        buffer.At(2, step, 0) = 86.0;
    }
    PathPricingMarketConfig market_config{};
    market_config.flat_fallbacks =
            PathPricingQuotes{.risk_free_rate = 0.03, .dividend_yield = 0.0, .flat_implied_volatility = 0.2};
    ScenarioSliceMarketData market(buffer, grid, BuildFactorIndexByUnderlying(kFactors), market_config,
                                   &resolver);

    std::vector<double> out;
    for (std::size_t step = 0; step < grid.NumSteps(); ++step) {
        market.SetSlice(step, 0);
        out.push_back(market.Quote(ticker));
    }
    return out;
}

}  // namespace

TEST(CommodityCurveResolverTest, ContractSitsExactlyOnItsPillarAtInception) {
    const fs::path path = SeedCurve();
    const auto grid = GridOf({kAsOf});

    const CommodityCurveResolver resolver = BuildCommodityCurveResolver(
            path.string(),
            {CommodityContractRef{
                    .contract_ticker = "CLX6", .product_code = "CL", .settlement_date = ParseIsoDate("2026-11-02")}},
            BuildFactorIndexByUnderlying(kFactors), grid, ParseIsoDate(kAsOf), 3);

    // Every listed contract *is* a pillar occupant on the as-of date, so inception
    // must reproduce its own settle rather than interpolate between neighbours.
    const std::vector<double> levels = ResolvedLevels(resolver, grid, "CLX6");
    ASSERT_EQ(levels.size(), 1U);
    EXPECT_DOUBLE_EQ(levels[0], 88.0);

    fs::remove(path);
}

TEST(CommodityCurveResolverTest, ContractRollsUpABackwardatedCurveAsItAges) {
    const fs::path path = SeedCurve();
    // CLX6 runs 61 days on as-of, so it starts on M2. It then ages through M1's
    // maturity (30 days) and finally sits inside the front pillar.
    const auto grid = GridOf({kAsOf, "2026-09-17", "2026-10-02", "2026-10-25"});

    const CommodityCurveResolver resolver = BuildCommodityCurveResolver(
            path.string(),
            {CommodityContractRef{
                    .contract_ticker = "CLX6", .product_code = "CL", .settlement_date = ParseIsoDate("2026-11-02")}},
            BuildFactorIndexByUnderlying(kFactors), grid, ParseIsoDate(kAsOf), 3);

    const std::vector<double> levels = ResolvedLevels(resolver, grid, "CLX6");
    ASSERT_EQ(levels.size(), 4U);

    // With the pillars held flat, the only thing moving the contract is its slide up
    // a backwardated curve — the roll return, which the mapping has to reproduce.
    EXPECT_DOUBLE_EQ(levels[0], 88.0);
    EXPECT_LT(levels[0], levels[1]);
    EXPECT_LT(levels[1], levels[2]);
    EXPECT_LT(levels[2], levels[3]);

    // Once nearer than the front pillar there is nothing left to blend against.
    EXPECT_DOUBLE_EQ(levels[3], 90.0);

    fs::remove(path);
}

TEST(CommodityCurveResolverTest, RejectsContractBeyondTheCalibratedStrip) {
    const fs::path path = SeedCurve();
    const auto grid = GridOf({kAsOf});
    const auto factor_map = BuildFactorIndexByUnderlying(kFactors);

    // Clamping here would silently price a far-dated leg with front-month dynamics.
    EXPECT_THROW(std::ignore = BuildCommodityCurveResolver(
                         path.string(),
                         {CommodityContractRef{.contract_ticker = "CLM7",
                                               .product_code = "CL",
                                               .settlement_date = ParseIsoDate("2027-06-01")}},
                         factor_map, grid, ParseIsoDate(kAsOf), 3),
                 numeraire::ValidationError);

    // A contract the strip does cover must still build, so the throw above is about
    // the maturity and not a strip that failed to load at all.
    EXPECT_NO_THROW(std::ignore = BuildCommodityCurveResolver(
            path.string(),
            {CommodityContractRef{
                    .contract_ticker = "CLZ6", .product_code = "CL", .settlement_date = ParseIsoDate("2026-11-30")}},
            factor_map, grid, ParseIsoDate(kAsOf), 3));

    fs::remove(path);
}

TEST(CommodityCurveResolverTest, QuoteResolvesDatedTickerThroughThePillars) {
    const fs::path path = SeedCurve();
    const auto grid = GridOf({kAsOf, "2026-09-17"});
    const auto factor_map = BuildFactorIndexByUnderlying(kFactors);

    const CommodityCurveResolver resolver = BuildCommodityCurveResolver(
            path.string(),
            {CommodityContractRef{
                    .contract_ticker = "CLX6", .product_code = "CL", .settlement_date = ParseIsoDate("2026-11-02")}},
            factor_map, grid, ParseIsoDate(kAsOf), 3);

    ScenarioBuffer buffer(3, grid.NumSteps(), 1);
    for (std::size_t step = 0; step < grid.NumSteps(); ++step) {
        buffer.At(0, step, 0) = 90.0;
        buffer.At(1, step, 0) = 88.0;
        buffer.At(2, step, 0) = 86.0;
    }

    PathPricingMarketConfig market_config{};
    market_config.flat_fallbacks =
            PathPricingQuotes{.risk_free_rate = 0.03, .dividend_yield = 0.0, .flat_implied_volatility = 0.2};
    ScenarioSliceMarketData market(buffer, grid, factor_map, market_config, &resolver);

    // At inception the dated ticker must return its own settle, not a blend.
    market.SetSlice(0, 0);
    EXPECT_DOUBLE_EQ(market.Quote("CLX6"), 88.0);

    // Aged past M2 but not yet inside M1, so strictly between their levels.
    market.SetSlice(1, 0);
    const double rolled = market.Quote("CLX6");
    EXPECT_GT(rolled, 88.0);
    EXPECT_LT(rolled, 90.0);

    // Pillar ids keep resolving directly alongside dated tickers.
    EXPECT_DOUBLE_EQ(market.Quote("CL_M3"), 86.0);

    fs::remove(path);
}

TEST(CommodityCurveResolverTest, UsesLastAvailableOccupancyWhenValuationDateHasNoQuotes) {
    numeraire::utils::Logger::Init();
    const fs::path path = SeedCurve();
    const auto grid = GridOf({"2026-09-07"});
    const auto factor_map = BuildFactorIndexByUnderlying(kFactors);

    // Same mapping as on the quoted session: CLX6 is still M2, so a later valuation
    // day without settles must not refuse to price.
    EXPECT_NO_THROW(std::ignore = BuildCommodityCurveResolver(
            path.string(),
            {CommodityContractRef{
                    .contract_ticker = "CLX6", .product_code = "CL", .settlement_date = ParseIsoDate("2026-11-02")}},
            factor_map, grid, ParseIsoDate("2026-09-07"), 3));

    const CommodityCurveResolver resolver = BuildCommodityCurveResolver(
            path.string(),
            {CommodityContractRef{
                    .contract_ticker = "CLX6", .product_code = "CL", .settlement_date = ParseIsoDate("2026-11-02")}},
            factor_map, grid, ParseIsoDate("2026-09-07"), 3);

    // Occupancy is frozen on 2026-09-02, so five calendar days of ageing slide CLX6
    // off M2 toward M1. The point of this test is that it still prices.
    const std::vector<double> levels = ResolvedLevels(resolver, grid, "CLX6");
    ASSERT_EQ(levels.size(), 1U);
    EXPECT_GT(levels[0], 88.0);
    EXPECT_LT(levels[0], 90.0);

    fs::remove(path);
}
