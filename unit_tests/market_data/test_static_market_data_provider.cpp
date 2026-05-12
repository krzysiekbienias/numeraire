#include <gtest/gtest.h>

#include <numeraire/core/imarket_data.hpp>
#include <numeraire/market_data/static_market_data_provider.hpp>
#include <numeraire/utils/exception.hpp>

TEST(StaticMarketDataProviderTest, CreatesMarketDataFromSnapshot) {
    numeraire::market_data::MarketSnapshot snap;
    snap.spots["AMZN"] = 211.65;
    snap.risk_free_rate = 0.03;
    snap.dividend_yields["AMZN"] = 0.01;
    snap.flat_implied_volatility = 0.25;

    const numeraire::market_data::StaticMarketDataProvider provider(std::move(snap));
    const std::unique_ptr<numeraire::core::IMarketData> mdt = provider.CreateMarketData();
    ASSERT_NE(mdt, nullptr);

    EXPECT_DOUBLE_EQ(mdt->Spot("AMZN"), 211.65);
    EXPECT_DOUBLE_EQ(mdt->RiskFreeRate(), 0.03);
    EXPECT_DOUBLE_EQ(mdt->DividendYield("AMZN"), 0.01);
    EXPECT_DOUBLE_EQ(mdt->ImpliedVolatility("AMZN", 200.0, 0.5), 0.25);
}

TEST(StaticMarketDataProviderTest, MissingUnderlyingSpotThrows) {
    numeraire::market_data::MarketSnapshot snap;
    snap.spots["QQQ"] = 400.0;
    const numeraire::market_data::StaticMarketDataProvider provider(std::move(snap));
    const std::unique_ptr<numeraire::core::IMarketData> mdt = provider.CreateMarketData();
    EXPECT_THROW(static_cast<void>(mdt->Spot("UNKNOWN")), numeraire::MarketDataError);
}

TEST(StaticMarketDataProviderTest, MissingDividendYieldIsZero) {
    numeraire::market_data::MarketSnapshot snap;
    snap.spots["SPY"] = 500.0;
    const numeraire::market_data::StaticMarketDataProvider provider(std::move(snap));
    const std::unique_ptr<numeraire::core::IMarketData> mdt = provider.CreateMarketData();
    EXPECT_DOUBLE_EQ(mdt->DividendYield("SPY"), 0.0);
}
