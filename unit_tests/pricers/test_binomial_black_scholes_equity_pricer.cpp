#include <gtest/gtest.h>

#include <numeraire/core/imarket_data.hpp>
#include <numeraire/enums/exercise_style.hpp>
#include <numeraire/enums/option_type.hpp>
#include <numeraire/enums/pricing_engine_type.hpp>
#include <numeraire/pricers/analytic_black_scholes_equity_pricer.hpp>
#include <numeraire/pricers/binomial_black_scholes_equity_pricer.hpp>
#include <numeraire/products/vanilla_equity_option_product.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/utils/exception.hpp>

#include <cmath>
#include <cstdlib>
#include <string>
#include <unordered_map>

namespace {

class MapMarket final : public numeraire::core::IMarketData {
   public:
    void SetValuationDate(const numeraire::schedule::Date& date) { valuation_date_ = date; }

    [[nodiscard]] const numeraire::schedule::Date& ValuationDate() const override { return valuation_date_; }

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

TEST(BinomialBlackScholesEquityPricerTest, EngineKindIsBinomialTree) {
    const numeraire::pricers::BinomialBlackScholesEquityPricer pricer;
    EXPECT_EQ(pricer.EngineKind(), numeraire::PricingEngineType::kBinomialTree);
}

TEST(BinomialBlackScholesEquityPricerTest, DefaultCtorReadsCommittedConfigSteps) {
    // scripts/test.sh runs from repo root so configs/default.json resolves.
    unsetenv("NUMERAIRE_BINOMIAL_STEPS");
    const numeraire::pricers::BinomialBlackScholesEquityPricer pricer;
    EXPECT_EQ(pricer.NSteps(), 200U);
}

TEST(BinomialBlackScholesEquityPricerTest, EnvOverridesCommittedConfigSteps) {
    ASSERT_EQ(setenv("NUMERAIRE_BINOMIAL_STEPS", "77", 1), 0);
    const numeraire::pricers::BinomialBlackScholesEquityPricer pricer;
    unsetenv("NUMERAIRE_BINOMIAL_STEPS");
    EXPECT_EQ(pricer.NSteps(), 77U);
}

TEST(BinomialBlackScholesEquityPricerTest, EuropeanCallNearAnalytic) {
    const numeraire::schedule::Date trade{.year = 2025, .month = 1, .day = 1};
    const numeraire::schedule::Date expiry{.year = 2026, .month = 1, .day = 1};
    const numeraire::products::VanillaEquityOptionProduct call("AAPL", numeraire::OptionType::kCall,
                                                               numeraire::ExerciseStyle::kEuropean, 100.0, trade,
                                                               expiry);

    MapMarket market;
    market.SetSpot("AAPL", 100.0);
    market.SetRate(0.05);
    market.SetDivYield(0.02);
    market.SetVol(0.25);

    const numeraire::pricers::AnalyticBlackScholesEquityPricer analytic;
    const numeraire::pricers::BinomialBlackScholesEquityPricer tree(400);
    const auto a = analytic.Price(call, market);
    const auto t = tree.Price(call, market);
    ASSERT_TRUE(a.Npv().has_value());
    ASSERT_TRUE(t.Npv().has_value());
    EXPECT_NEAR(*t.Npv(), *a.Npv(), 0.05);
}

TEST(BinomialBlackScholesEquityPricerTest, AmericanPutAboveEuropean) {
    const numeraire::schedule::Date trade{.year = 2025, .month = 1, .day = 1};
    const numeraire::schedule::Date expiry{.year = 2026, .month = 1, .day = 1};

    const numeraire::products::VanillaEquityOptionProduct eu("AAPL", numeraire::OptionType::kPut,
                                                             numeraire::ExerciseStyle::kEuropean, 100.0, trade,
                                                             expiry);
    const numeraire::products::VanillaEquityOptionProduct am("AAPL", numeraire::OptionType::kPut,
                                                             numeraire::ExerciseStyle::kAmerican, 100.0, trade,
                                                             expiry);

    MapMarket market;
    market.SetSpot("AAPL", 100.0);
    market.SetRate(0.05);
    market.SetDivYield(0.0);
    market.SetVol(0.25);

    const numeraire::pricers::BinomialBlackScholesEquityPricer pricer(200);
    const auto eu_res = pricer.Price(eu, market);
    const auto am_res = pricer.Price(am, market);
    ASSERT_TRUE(eu_res.Npv().has_value());
    ASSERT_TRUE(am_res.Npv().has_value());
    EXPECT_GT(*am_res.Npv(), *eu_res.Npv() + 0.05);
}

TEST(BinomialBlackScholesEquityPricerTest, AmericanProducesBumpGreeks) {
    const numeraire::schedule::Date trade{.year = 2025, .month = 1, .day = 1};
    const numeraire::schedule::Date expiry{.year = 2025, .month = 7, .day = 1};
    const numeraire::products::VanillaEquityOptionProduct put("AAPL", numeraire::OptionType::kPut,
                                                              numeraire::ExerciseStyle::kAmerican, 100.0, trade,
                                                              expiry);

    MapMarket market;
    market.SetSpot("AAPL", 100.0);
    market.SetRate(0.04);
    market.SetDivYield(0.0);
    market.SetVol(0.22);

    const numeraire::pricers::BinomialBlackScholesEquityPricer pricer(120);
    const auto res = pricer.Price(put, market);
    ASSERT_TRUE(res.Npv().has_value());
    ASSERT_TRUE(res.Greeks().has_value());
    EXPECT_TRUE(res.Greeks()->delta.has_value());
    EXPECT_TRUE(res.Greeks()->gamma.has_value());
    EXPECT_TRUE(res.Greeks()->vega.has_value());
    EXPECT_TRUE(res.Greeks()->theta.has_value());
    EXPECT_TRUE(res.Greeks()->rho.has_value());
    EXPECT_LT(*res.Greeks()->delta, 0.0);
    EXPECT_GT(*res.Greeks()->gamma, 0.0);
}

TEST(BinomialBlackScholesEquityPricerTest, ZeroTimeIsIntrinsic) {
    const numeraire::schedule::Date d{.year = 2025, .month = 6, .day = 1};
    const numeraire::products::VanillaEquityOptionProduct call("AAPL", numeraire::OptionType::kCall,
                                                               numeraire::ExerciseStyle::kAmerican, 100.0, d, d);

    MapMarket market;
    market.SetValuationDate(d);
    market.SetSpot("AAPL", 112.0);
    market.SetRate(0.05);
    market.SetVol(0.2);

    const numeraire::pricers::BinomialBlackScholesEquityPricer pricer;
    const auto res = pricer.Price(call, market);
    ASSERT_TRUE(res.Npv().has_value());
    EXPECT_NEAR(*res.Npv(), 12.0, 1.0e-12);
}
