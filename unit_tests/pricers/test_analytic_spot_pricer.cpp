#include <gtest/gtest.h>

#include <numeraire/core/imarket_data.hpp>
#include <numeraire/enums/exercise_style.hpp>
#include <numeraire/enums/option_type.hpp>
#include <numeraire/pricers/analytic_composite_pricer.hpp>
#include <numeraire/pricers/analytic_spot_pricer.hpp>
#include <numeraire/products/equity_spot_product.hpp>
#include <numeraire/products/vanilla_equity_option_product.hpp>
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
    numeraire::schedule::Date valuation_date_{.year = 2025, .month = 6, .day = 15};
};

}  // namespace

TEST(AnalyticSpotPricerTest, MarksToMarketSpotWithUnitDelta) {
    MapMarket m;
    m.SetSpot("AAPL", 187.5);

    const numeraire::schedule::Date trade{.year = 2025, .month = 6, .day = 1};
    const numeraire::products::EquitySpotProduct spot("AAPL", trade);
    const numeraire::pricers::AnalyticSpotPricer pricer;
    const numeraire::core::PricingResult out = pricer.Price(spot, m);

    ASSERT_TRUE(out.Npv().has_value());
    EXPECT_DOUBLE_EQ(*out.Npv(), 187.5);
    ASSERT_TRUE(out.Greeks().has_value());
    ASSERT_TRUE(out.Greeks()->delta.has_value());
    EXPECT_DOUBLE_EQ(*out.Greeks()->delta, 1.0);
    EXPECT_DOUBLE_EQ(*out.Greeks()->gamma, 0.0);
}

TEST(AnalyticSpotPricerTest, RejectsVanillaOption) {
    MapMarket m;
    m.SetSpot("AAPL", 100.0);
    const numeraire::schedule::Date d{.year = 2025, .month = 6, .day = 15};
    const numeraire::products::VanillaEquityOptionProduct opt(
            "AAPL", numeraire::OptionType::kCall, numeraire::ExerciseStyle::kEuropean, 100.0, d, d);
    const numeraire::pricers::AnalyticSpotPricer pricer;
    EXPECT_THROW(static_cast<void>(pricer.Price(opt, m)), numeraire::ValidationError);
}

TEST(AnalyticCompositePricerTest, RoutesSpotToSpotPricer) {
    MapMarket m;
    m.SetSpot("NDX", 21000.0);

    const numeraire::schedule::Date trade{.year = 2025, .month = 1, .day = 1};
    const numeraire::products::EquitySpotProduct spot("NDX", trade);
    const numeraire::pricers::AnalyticCompositePricer composite;
    const numeraire::core::PricingResult out = composite.Price(spot, m);

    ASSERT_TRUE(out.Npv().has_value());
    EXPECT_DOUBLE_EQ(*out.Npv(), 21000.0);
    ASSERT_TRUE(out.Greeks().has_value());
    EXPECT_DOUBLE_EQ(*out.Greeks()->delta, 1.0);
}
