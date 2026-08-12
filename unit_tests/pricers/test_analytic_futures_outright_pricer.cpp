#include <gtest/gtest.h>

#include <numeraire/core/imarket_data.hpp>
#include <numeraire/pricers/analytic_composite_pricer.hpp>
#include <numeraire/pricers/analytic_futures_outright_pricer.hpp>
#include <numeraire/products/commodity_futures_outright_product.hpp>
#include <numeraire/products/equity_spot_product.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/utils/exception.hpp>

#include <string>
#include <unordered_map>

namespace {

class MapMarket final : public numeraire::core::IMarketData {
   public:
    void SetValuationDate(const numeraire::schedule::Date& date) { valuation_date_ = date; }

    [[nodiscard]] const numeraire::schedule::Date& ValuationDate() const override {
        return valuation_date_;
    }

    [[nodiscard]] double Spot(const std::string_view underlying_id) const override {
        return spots_.at(std::string(underlying_id));
    }

    [[nodiscard]] double RiskFreeRate() const override { return 0.05; }

    [[nodiscard]] double DividendYield(const std::string_view) const override { return 0.0; }

    [[nodiscard]] double ImpliedVolatility(const std::string_view, const double, const double,
                                           const numeraire::OptionType) const override {
        return 0.2;
    }

    void SetSpot(std::string id, const double v) { spots_[std::move(id)] = v; }

   private:
    std::unordered_map<std::string, double> spots_;
    numeraire::schedule::Date valuation_date_{.year = 2026, .month = 8, .day = 11};
};

}  // namespace

TEST(AnalyticFuturesOutrightPricerTest, MarksToSettlementWithUnitDelta) {
    MapMarket m;
    m.SetSpot("CLX6", 80.31);

    const numeraire::schedule::Date trade{.year = 2026, .month = 8, .day = 11};
    const numeraire::schedule::Date expiry{.year = 2026, .month = 10, .day = 20};
    const numeraire::products::CommodityFuturesOutrightProduct fut("CLX6", "CL", trade, expiry);
    const numeraire::pricers::AnalyticFuturesOutrightPricer pricer;
    const numeraire::core::PricingResult out = pricer.Price(fut, m);

    ASSERT_TRUE(out.Npv().has_value());
    EXPECT_DOUBLE_EQ(*out.Npv(), 80.31);
    ASSERT_TRUE(out.Greeks().has_value());
    ASSERT_TRUE(out.Greeks()->delta.has_value());
    EXPECT_DOUBLE_EQ(*out.Greeks()->delta, 1.0);
}

TEST(AnalyticFuturesOutrightPricerTest, RejectsEquitySpot) {
    MapMarket m;
    m.SetSpot("AAPL", 100.0);
    const numeraire::schedule::Date d{.year = 2026, .month = 8, .day = 11};
    const numeraire::products::EquitySpotProduct spot("AAPL", d);
    const numeraire::pricers::AnalyticFuturesOutrightPricer pricer;
    EXPECT_THROW(static_cast<void>(pricer.Price(spot, m)), numeraire::ValidationError);
}

TEST(AnalyticCompositePricerTest, RoutesFuturesOutright) {
    MapMarket m;
    m.SetSpot("CLX6", 80.31);
    const numeraire::schedule::Date trade{.year = 2026, .month = 8, .day = 11};
    const numeraire::schedule::Date expiry{.year = 2026, .month = 10, .day = 20};
    const numeraire::products::CommodityFuturesOutrightProduct fut("CLX6", "CL", trade, expiry);
    const numeraire::pricers::AnalyticCompositePricer composite;
    const numeraire::core::PricingResult out = composite.Price(fut, m);
    ASSERT_TRUE(out.Npv().has_value());
    EXPECT_DOUBLE_EQ(*out.Npv(), 80.31);
}
