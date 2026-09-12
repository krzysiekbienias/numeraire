#include <gtest/gtest.h>

#include <cmath>
#include <numeraire/core/imarket_data.hpp>
#include <numeraire/pricers/analytic_commodity_futures_forward_pricer.hpp>
#include <numeraire/pricers/analytic_composite_pricer.hpp>
#include <numeraire/products/commodity_futures_forward_product.hpp>
#include <numeraire/products/commodity_futures_outright_product.hpp>
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

    [[nodiscard]] double RiskFreeRate() const override { return r_; }

    [[nodiscard]] double DividendYield(const std::string_view) const override { return 0.0; }

    [[nodiscard]] double ImpliedVolatility(const std::string_view, const double, const double,
                                           const numeraire::OptionType) const override {
        return 0.2;
    }

    void SetSpot(std::string id, const double v) { spots_[std::move(id)] = v; }

    void SetRate(const double r) { r_ = r; }

   private:
    std::unordered_map<std::string, double> spots_;
    double r_ = 0.0;
    numeraire::schedule::Date valuation_date_{.year = 2026, .month = 8, .day = 11};
};

}  // namespace

TEST(AnalyticCommodityFuturesForwardPricerTest, DiscountedFuturesMinusStrike) {
    const numeraire::schedule::Date trade{.year = 2026, .month = 8, .day = 11};
    const numeraire::schedule::Date expiry{.year = 2026, .month = 10, .day = 20};
    const double tau = numeraire::schedule::Act365FixedYearFraction(trade, expiry);

    MapMarket m;
    m.SetValuationDate(trade);
    m.SetSpot("CLX6", 98.40);
    m.SetRate(0.05);

    const numeraire::products::CommodityFuturesForwardProduct fwd("CLX6", "CL", 80.31, trade, expiry);
    const numeraire::pricers::AnalyticCommodityFuturesForwardPricer pricer;
    const numeraire::core::PricingResult out = pricer.Price(fwd, m);

    const double df = std::exp(-0.05 * tau);
    ASSERT_TRUE(out.Npv().has_value());
    EXPECT_NEAR(*out.Npv(), df * (98.40 - 80.31), 1e-12);
    ASSERT_TRUE(out.Greeks().has_value());
    ASSERT_TRUE(out.Greeks()->delta.has_value());
    EXPECT_NEAR(*out.Greeks()->delta, df, 1e-12);
}

TEST(AnalyticCommodityFuturesForwardPricerTest, ZeroTimeIsFuturesMinusStrike) {
    const numeraire::schedule::Date d{.year = 2026, .month = 10, .day = 20};
    MapMarket m;
    m.SetValuationDate(d);
    m.SetSpot("CLX6", 98.40);
    m.SetRate(0.05);

    const numeraire::products::CommodityFuturesForwardProduct fwd("CLX6", "CL", 80.31, d, d);
    const numeraire::pricers::AnalyticCommodityFuturesForwardPricer pricer;
    const numeraire::core::PricingResult out = pricer.Price(fwd, m);

    ASSERT_TRUE(out.Npv().has_value());
    EXPECT_DOUBLE_EQ(*out.Npv(), 18.09);
    ASSERT_TRUE(out.Greeks().has_value());
    EXPECT_DOUBLE_EQ(*out.Greeks()->delta, 1.0);
}

TEST(AnalyticCommodityFuturesForwardPricerTest, RejectsOutright) {
    MapMarket m;
    m.SetSpot("CLX6", 80.31);
    const numeraire::schedule::Date trade{.year = 2026, .month = 8, .day = 11};
    const numeraire::schedule::Date expiry{.year = 2026, .month = 10, .day = 20};
    const numeraire::products::CommodityFuturesOutrightProduct outright("CLX6", "CL", trade, expiry);
    const numeraire::pricers::AnalyticCommodityFuturesForwardPricer pricer;
    EXPECT_THROW(static_cast<void>(pricer.Price(outright, m)), numeraire::ValidationError);
}

TEST(AnalyticCompositePricerTest, RoutesCommodityFuturesForward) {
    MapMarket m;
    m.SetSpot("CLX6", 80.31);
    m.SetRate(0.0);
    const numeraire::schedule::Date trade{.year = 2026, .month = 8, .day = 11};
    const numeraire::schedule::Date expiry{.year = 2026, .month = 10, .day = 20};
    const numeraire::products::CommodityFuturesForwardProduct fwd("CLX6", "CL", 80.31, trade, expiry);
    const numeraire::pricers::AnalyticCompositePricer composite;
    const numeraire::core::PricingResult out = composite.Price(fwd, m);
    ASSERT_TRUE(out.Npv().has_value());
    EXPECT_DOUBLE_EQ(*out.Npv(), 0.0);
}
