#include <gtest/gtest.h>

#include <numeraire/core/imarket_data.hpp>
#include <numeraire/enums/exercise_style.hpp>
#include <numeraire/enums/option_type.hpp>
#include <numeraire/pricers/analytic_composite_pricer.hpp>
#include <numeraire/products/equity_forward_product.hpp>
#include <numeraire/products/vanilla_equity_option_product.hpp>
#include <numeraire/schedule/date.hpp>
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

    [[nodiscard]] double DividendYield(const std::string_view underlying_id) const override {
        static_cast<void>(underlying_id);
        return q_;
    }

    [[nodiscard]] double ImpliedVolatility(const std::string_view underlying_id,
                                           const double strike,
                                           const double time_to_expiry_years,
                                           const numeraire::OptionType option_kind) const override {
        static_cast<void>(underlying_id);
        static_cast<void>(strike);
        static_cast<void>(time_to_expiry_years);
        static_cast<void>(option_kind);
        return vol_;
    }

    void SetSpot(std::string id, const double v) { spots_[std::move(id)] = v; }

    void SetRate(const double r) { r_ = r; }

    void SetDivYield(const double q) { q_ = q; }

    void SetVol(const double v) { vol_ = v; }

   private:
    std::unordered_map<std::string, double> spots_;
    double r_ = 0.0;
    double q_ = 0.0;
    double vol_ = 0.2;
    numeraire::schedule::Date valuation_date_{.year = 2025, .month = 1, .day = 1};
};

}  // namespace

TEST(AnalyticCompositePricerTest, RoutesVanillaToBlackScholesPricer) {
    const numeraire::schedule::Date trade{.year = 2025, .month = 1, .day = 1};
    const numeraire::schedule::Date expiry{.year = 2026, .month = 1, .day = 1};

    MapMarket m;
    m.SetValuationDate(trade);
    m.SetSpot("SPX", 100.0);
    m.SetRate(0.05);
    m.SetDivYield(0.0);
    m.SetVol(0.25);

    const numeraire::products::VanillaEquityOptionProduct opt(
            "SPX", numeraire::OptionType::kCall, numeraire::ExerciseStyle::kEuropean, 100.0, trade, expiry);

    const numeraire::pricers::AnalyticCompositePricer composite;
    const numeraire::core::PricingResult out = composite.Price(opt, m);

    ASSERT_TRUE(out.Npv().has_value());
    EXPECT_GT(*out.Npv(), 0.0);
    ASSERT_TRUE(out.Greeks().has_value());
}

TEST(AnalyticCompositePricerTest, RoutesForwardToForwardPricer) {
    const numeraire::schedule::Date trade{.year = 2025, .month = 1, .day = 1};
    const numeraire::schedule::Date expiry{.year = 2026, .month = 1, .day = 1};

    MapMarket m;
    m.SetValuationDate(trade);
    m.SetSpot("AAPL", 100.0);
    m.SetRate(0.05);
    m.SetDivYield(0.02);

    const numeraire::products::EquityForwardProduct forward("AAPL", 95.0, trade, expiry);

    const numeraire::pricers::AnalyticCompositePricer composite;
    const numeraire::core::PricingResult out = composite.Price(forward, m);

    ASSERT_TRUE(out.Npv().has_value());
    EXPECT_FALSE(out.Greeks().has_value());
}

TEST(AnalyticCompositePricerTest, EngineKindIsAnalytic) {
    const numeraire::pricers::AnalyticCompositePricer pricer;
    EXPECT_EQ(pricer.EngineKind(), numeraire::PricingEngineType::kAnalytic);
}
