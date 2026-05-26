#include <gtest/gtest.h>

#include <cmath>
#include <numeraire/core/imarket_data.hpp>
#include <numeraire/pricers/analytic_forward_pricer.hpp>
#include <numeraire/products/equity_forward_product.hpp>
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

    [[nodiscard]] double RiskFreeRate() const override { return r_; }

    [[nodiscard]] double DividendYield(const std::string_view underlying_id) const override {
        static_cast<void>(underlying_id);
        return q_;
    }

    [[nodiscard]] double ImpliedVolatility(const std::string_view underlying_id,
                                           const double strike,
                                           const double time_to_expiry_years) const override {
        static_cast<void>(underlying_id);
        static_cast<void>(strike);
        static_cast<void>(time_to_expiry_years);
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

TEST(AnalyticForwardPricerTest, EquityForwardMatchesClosedForm) {
    const numeraire::schedule::Date trade{.year = 2025, .month = 1, .day = 1};
    const numeraire::schedule::Date expiry{.year = 2026, .month = 1, .day = 1};
    const double tau = numeraire::schedule::Act365FixedYearFraction(trade, expiry);

    MapMarket m;
    m.SetValuationDate(trade);
    m.SetSpot("AAPL", 100.0);
    m.SetRate(0.05);
    m.SetDivYield(0.02);

    const numeraire::products::EquityForwardProduct forward("AAPL", 95.0, trade, expiry);
    const numeraire::pricers::AnalyticForwardPricer pricer;
    const numeraire::core::PricingResult out = pricer.Price(forward, m);

    const double expected = (100.0 * std::exp(-0.02 * tau)) - (95.0 * std::exp(-0.05 * tau));
    ASSERT_TRUE(out.Npv().has_value());
    EXPECT_NEAR(*out.Npv(), expected, 1e-12);
    EXPECT_FALSE(out.Greeks().has_value());
}

TEST(AnalyticForwardPricerTest, ZeroTimeIsSpotMinusForwardPrice) {
    const numeraire::schedule::Date d{.year = 2025, .month = 6, .day = 15};

    MapMarket m;
    m.SetValuationDate(d);
    m.SetSpot("AAPL", 105.0);
    m.SetRate(0.05);

    const numeraire::products::EquityForwardProduct forward("AAPL", 100.0, d, d);
    const numeraire::pricers::AnalyticForwardPricer pricer;
    const numeraire::core::PricingResult out = pricer.Price(forward, m);

    ASSERT_TRUE(out.Npv().has_value());
    EXPECT_DOUBLE_EQ(*out.Npv(), 5.0);
}

TEST(AnalyticForwardPricerTest, IgnoresVol) {
    const numeraire::schedule::Date trade{.year = 2025, .month = 1, .day = 1};
    const numeraire::schedule::Date expiry{.year = 2026, .month = 1, .day = 1};

    MapMarket low_vol;
    low_vol.SetValuationDate(trade);
    low_vol.SetSpot("MSFT", 420.0);
    low_vol.SetRate(0.03);
    low_vol.SetVol(0.05);

    MapMarket high_vol;
    high_vol.SetValuationDate(trade);
    high_vol.SetSpot("MSFT", 420.0);
    high_vol.SetRate(0.03);
    high_vol.SetVol(0.80);

    const numeraire::products::EquityForwardProduct forward("MSFT", 400.0, trade, expiry);
    const numeraire::pricers::AnalyticForwardPricer pricer;

    const numeraire::core::PricingResult low = pricer.Price(forward, low_vol);
    const numeraire::core::PricingResult high = pricer.Price(forward, high_vol);

    ASSERT_TRUE(low.Npv().has_value());
    ASSERT_TRUE(high.Npv().has_value());
    EXPECT_DOUBLE_EQ(*low.Npv(), *high.Npv());
}

TEST(AnalyticForwardPricerTest, RejectsVanillaOption) {
    const numeraire::schedule::Date d{.year = 2025, .month = 6, .day = 15};

    MapMarket m;
    m.SetValuationDate(d);
    m.SetSpot("AAPL", 100.0);

    const numeraire::products::VanillaEquityOptionProduct opt(
            "AAPL", numeraire::OptionType::kCall, numeraire::ExerciseStyle::kEuropean, 100.0, d, d);
    const numeraire::pricers::AnalyticForwardPricer pricer;
    EXPECT_THROW(static_cast<void>(pricer.Price(opt, m)), numeraire::ValidationError);
}

TEST(AnalyticForwardPricerTest, EngineKindIsAnalytic) {
    const numeraire::pricers::AnalyticForwardPricer pricer;
    EXPECT_EQ(pricer.EngineKind(), numeraire::PricingEngineType::kAnalytic);
}
