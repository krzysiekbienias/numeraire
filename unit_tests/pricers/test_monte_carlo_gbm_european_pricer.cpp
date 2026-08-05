#include <gtest/gtest.h>

#include <numeraire/core/imarket_data.hpp>
#include <numeraire/enums/exercise_style.hpp>
#include <numeraire/enums/option_type.hpp>
#include <numeraire/enums/pricing_engine_type.hpp>
#include <numeraire/pricers/analytic_black_scholes_equity_pricer.hpp>
#include <numeraire/pricers/monte_carlo_gbm_european_pricer.hpp>
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

void ConfigureAtmMarket(MapMarket& market) {
    market.SetSpot("AAPL", 100.0);
    market.SetRate(0.05);
    market.SetDivYield(0.02);
    market.SetVol(0.25);
}

[[nodiscard]] numeraire::products::VanillaEquityOptionProduct OneYearCall(
        const numeraire::ExerciseStyle exercise = numeraire::ExerciseStyle::kEuropean) {
    const numeraire::schedule::Date trade{.year = 2025, .month = 1, .day = 1};
    const numeraire::schedule::Date expiry{.year = 2026, .month = 1, .day = 1};
    return numeraire::products::VanillaEquityOptionProduct(
            "AAPL", numeraire::OptionType::kCall, exercise, 100.0, trade, expiry);
}

}  // namespace

TEST(MonteCarloGbmEuropeanPricerTest, EngineKindIsMonteCarlo) {
    const numeraire::pricers::MonteCarloGbmEuropeanPricer pricer;
    EXPECT_EQ(pricer.EngineKind(), numeraire::PricingEngineType::kMonteCarlo);
}

TEST(MonteCarloGbmEuropeanPricerTest, EnvOverridesPathsAndSeed) {
    ASSERT_EQ(setenv("NUMERAIRE_MC_PRICER_PATHS", "4096", 1), 0);
    ASSERT_EQ(setenv("NUMERAIRE_MC_PRICER_SEED", "7", 1), 0);
    const numeraire::pricers::MonteCarloGbmEuropeanPricer pricer;
    unsetenv("NUMERAIRE_MC_PRICER_PATHS");
    unsetenv("NUMERAIRE_MC_PRICER_SEED");
    EXPECT_EQ(pricer.NumPaths(), 4096U);
    EXPECT_EQ(pricer.Seed(), 7U);
}

TEST(MonteCarloGbmEuropeanPricerTest, EuropeanCallConvergesToAnalytic) {
    const auto call = OneYearCall();
    MapMarket market;
    ConfigureAtmMarket(market);

    const numeraire::pricers::AnalyticBlackScholesEquityPricer analytic;
    const numeraire::pricers::MonteCarloGbmEuropeanPricer monte_carlo(500000, 20240501);
    const auto a = analytic.Price(call, market);
    const auto m = monte_carlo.Price(call, market);
    ASSERT_TRUE(a.Npv().has_value());
    ASSERT_TRUE(m.Npv().has_value());

    // Payoff standard deviation here is ~17, so 500k paths give a standard error of
    // ~0.025. The seed is fixed, so this bound is deterministic, not flaky.
    EXPECT_NEAR(*m.Npv(), *a.Npv(), 0.15);
}

TEST(MonteCarloGbmEuropeanPricerTest, MorePathsShrinkTheError) {
    const auto call = OneYearCall();
    MapMarket market;
    ConfigureAtmMarket(market);

    const numeraire::pricers::AnalyticBlackScholesEquityPricer analytic;
    const auto reference = analytic.Price(call, market);
    ASSERT_TRUE(reference.Npv().has_value());

    const numeraire::pricers::MonteCarloGbmEuropeanPricer few(2000, 20240501);
    const numeraire::pricers::MonteCarloGbmEuropeanPricer many(2000000, 20240501);
    const auto coarse = few.Price(call, market);
    const auto fine = many.Price(call, market);
    ASSERT_TRUE(coarse.Npv().has_value());
    ASSERT_TRUE(fine.Npv().has_value());

    EXPECT_LT(std::abs(*fine.Npv() - *reference.Npv()), std::abs(*coarse.Npv() - *reference.Npv()));
}

TEST(MonteCarloGbmEuropeanPricerTest, SameSeedReproducesPriceAndSeedsDiffer) {
    const auto call = OneYearCall();
    MapMarket market;
    ConfigureAtmMarket(market);

    const numeraire::pricers::MonteCarloGbmEuropeanPricer first(50000, 99);
    const numeraire::pricers::MonteCarloGbmEuropeanPricer repeat(50000, 99);
    const numeraire::pricers::MonteCarloGbmEuropeanPricer other(50000, 100);

    const auto a = first.Price(call, market);
    const auto b = repeat.Price(call, market);
    const auto c = other.Price(call, market);
    ASSERT_TRUE(a.Npv().has_value());
    ASSERT_TRUE(b.Npv().has_value());
    ASSERT_TRUE(c.Npv().has_value());

    EXPECT_DOUBLE_EQ(*a.Npv(), *b.Npv());
    EXPECT_NE(*a.Npv(), *c.Npv());
}

TEST(MonteCarloGbmEuropeanPricerTest, DiagnosticsCarryPathsSeedAndStandardError) {
    const auto call = OneYearCall();
    MapMarket market;
    ConfigureAtmMarket(market);

    const numeraire::pricers::MonteCarloGbmEuropeanPricer pricer(8192, 31337);
    const auto res = pricer.Price(call, market);
    ASSERT_TRUE(res.Metadata().has_value());
    ASSERT_TRUE(res.Metadata()->diagnostics.has_value());
    const std::string& diagnostics = *res.Metadata()->diagnostics;
    EXPECT_NE(diagnostics.find("mc_paths=8192"), std::string::npos);
    EXPECT_NE(diagnostics.find("mc_seed=31337"), std::string::npos);
    EXPECT_NE(diagnostics.find("mc_std_err="), std::string::npos);
}

TEST(MonteCarloGbmEuropeanPricerTest, NoGreeksReported) {
    const auto call = OneYearCall();
    MapMarket market;
    ConfigureAtmMarket(market);

    const numeraire::pricers::MonteCarloGbmEuropeanPricer pricer(4096, 1);
    const auto res = pricer.Price(call, market);
    EXPECT_FALSE(res.Greeks().has_value());
}

TEST(MonteCarloGbmEuropeanPricerTest, AmericanExerciseThrows) {
    const auto american = OneYearCall(numeraire::ExerciseStyle::kAmerican);
    MapMarket market;
    ConfigureAtmMarket(market);

    const numeraire::pricers::MonteCarloGbmEuropeanPricer pricer(1024, 1);
    EXPECT_THROW(static_cast<void>(pricer.Price(american, market)), numeraire::ValidationError);
}

TEST(MonteCarloGbmEuropeanPricerTest, ZeroTimeIsIntrinsic) {
    const numeraire::schedule::Date d{.year = 2025, .month = 6, .day = 1};
    const numeraire::products::VanillaEquityOptionProduct call("AAPL", numeraire::OptionType::kCall,
                                                               numeraire::ExerciseStyle::kEuropean, 100.0, d, d);

    MapMarket market;
    market.SetValuationDate(d);
    market.SetSpot("AAPL", 112.0);
    market.SetRate(0.05);
    market.SetVol(0.2);

    const numeraire::pricers::MonteCarloGbmEuropeanPricer pricer(1024, 1);
    const auto res = pricer.Price(call, market);
    ASSERT_TRUE(res.Npv().has_value());
    EXPECT_NEAR(*res.Npv(), 12.0, 1.0e-12);
}
