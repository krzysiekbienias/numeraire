#include <gtest/gtest.h>

#include <numeraire/core/imarket_data.hpp>
#include <numeraire/core/iproduct.hpp>
#include <numeraire/enums/exercise_style.hpp>
#include <numeraire/enums/option_type.hpp>
#include <numeraire/pricers/analytic_black_scholes_equity_pricer.hpp>
#include <numeraire/products/vanilla_equity_option_product.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/utils/exception.hpp>
#include <numeraire/utils/quantlib_bridge.hpp>

#include <ql/pricingengines/blackcalculator.hpp>

#include <cmath>
#include <string>
#include <unordered_map>

namespace {

class MapMarket final : public numeraire::core::IMarketData {
   public:
    [[nodiscard]] double Spot(const std::string_view underlying_id) const override {
        return spots_.at(std::string(underlying_id));
    }

    [[nodiscard]] double RiskFreeRate() const override { return r_; }

    [[nodiscard]] double DividendYield(const std::string_view underlying_id) const override {
        static_cast<void>(underlying_id);
        return q_;
    }

    [[nodiscard]] double ImpliedVolatility(const std::string_view underlying_id, const double strike,
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
};

class WrongProduct final : public numeraire::core::IProduct {
   public:
    [[nodiscard]] std::string_view UnderlyingId() const override { return "X"; }

    [[nodiscard]] numeraire::OptionType OptionKind() const override {
        return numeraire::OptionType::kCall;
    }

    [[nodiscard]] numeraire::ExerciseStyle Exercise() const override {
        return numeraire::ExerciseStyle::kEuropean;
    }

    [[nodiscard]] double Strike() const override { return 100.0; }

    [[nodiscard]] const numeraire::schedule::Date& TradeDate() const override { return d_; }

    [[nodiscard]] const numeraire::schedule::Date& ExpiryDate() const override { return d_; }

    [[nodiscard]] const numeraire::schedule::Schedule* PaymentSchedule() const override {
        return nullptr;
    }

   private:
    numeraire::schedule::Date d_{.year = 2025, .month = 6, .day = 1};
};

/// Benchmark NPV / greeks via QuantLib Black-76 on forward (equivalent to BS on spot).
[[nodiscard]] QuantLib::BlackCalculator BenchBlack(const numeraire::OptionType kind, const double strike,
                                                    const double spot, const double r, const double q,
                                                    const double vol, const double tau) {
    const double forward = spot * std::exp((r - q) * tau);
    const double std_dev = vol * std::sqrt(tau);
    const double discount = std::exp(-r * tau);
    return QuantLib::BlackCalculator(numeraire::utils::quantlib_bridge::ToQuantLib(kind), strike, forward,
                                     std_dev, discount);
}

}  // namespace

TEST(AnalyticBlackScholesEquityPricerTest, EuropeanCallMatchesQuantLibBenchmark) {
    MapMarket m;
    m.SetSpot("SPX", 100.0);
    m.SetRate(0.05);
    m.SetDivYield(0.02);
    m.SetVol(0.25);

    const numeraire::schedule::Date trade{.year = 2025, .month = 1, .day = 1};
    const numeraire::schedule::Date expiry{.year = 2026, .month = 1, .day = 1};
    const double tau = numeraire::schedule::Act365FixedYearFraction(trade, expiry);

    const numeraire::products::VanillaEquityOptionProduct opt("SPX", numeraire::OptionType::kCall,
                                                              numeraire::ExerciseStyle::kEuropean, 100.0,
                                                              trade, expiry);

    const numeraire::pricers::AnalyticBlackScholesEquityPricer pricer;
    const numeraire::core::PricingResult out = pricer.Price(opt, m);

    const QuantLib::BlackCalculator bench =
            BenchBlack(numeraire::OptionType::kCall, 100.0, 100.0, 0.05, 0.02, 0.25, tau);

    ASSERT_TRUE(out.Npv().has_value());
    EXPECT_NEAR(*out.Npv(), bench.value(), 1e-12);
    ASSERT_TRUE(out.Greeks().has_value());
    ASSERT_TRUE(out.Greeks()->delta.has_value());
    EXPECT_NEAR(*out.Greeks()->delta, bench.delta(100.0), 1e-10);
    EXPECT_NEAR(*out.Greeks()->gamma, bench.gamma(100.0), 1e-10);
    EXPECT_NEAR(*out.Greeks()->vega, bench.vega(tau), 1e-10);
    EXPECT_NEAR(*out.Greeks()->theta, bench.theta(100.0, tau), 1e-10);
    EXPECT_NEAR(*out.Greeks()->rho, bench.rho(tau), 1e-10);
}

TEST(AnalyticBlackScholesEquityPricerTest, EuropeanPutMatchesQuantLibBenchmark) {
    MapMarket m;
    m.SetSpot("SPX", 100.0);
    m.SetRate(0.05);
    m.SetDivYield(0.02);
    m.SetVol(0.25);

    const numeraire::schedule::Date trade{.year = 2025, .month = 1, .day = 1};
    const numeraire::schedule::Date expiry{.year = 2026, .month = 1, .day = 1};
    const double tau = numeraire::schedule::Act365FixedYearFraction(trade, expiry);

    const numeraire::products::VanillaEquityOptionProduct opt(
            "SPX", numeraire::OptionType::kPut, numeraire::ExerciseStyle::kEuropean, 100.0, trade, expiry);

    const numeraire::pricers::AnalyticBlackScholesEquityPricer pricer;
    const numeraire::core::PricingResult out = pricer.Price(opt, m);

    const QuantLib::BlackCalculator bench =
            BenchBlack(numeraire::OptionType::kPut, 100.0, 100.0, 0.05, 0.02, 0.25, tau);

    ASSERT_TRUE(out.Npv().has_value());
    EXPECT_NEAR(*out.Npv(), bench.value(), 1e-12);
    ASSERT_TRUE(out.Greeks().has_value());
    EXPECT_NEAR(*out.Greeks()->delta, bench.delta(100.0), 1e-10);
    EXPECT_NEAR(*out.Greeks()->gamma, bench.gamma(100.0), 1e-10);
    EXPECT_NEAR(*out.Greeks()->vega, bench.vega(tau), 1e-10);
    EXPECT_NEAR(*out.Greeks()->theta, bench.theta(100.0, tau), 1e-10);
    EXPECT_NEAR(*out.Greeks()->rho, bench.rho(tau), 1e-10);
}

TEST(AnalyticBlackScholesEquityPricerTest, ZeroTimeIsIntrinsic) {
    MapMarket m;
    m.SetSpot("SPX", 105.0);
    m.SetRate(0.05);
    m.SetVol(0.2);

    const numeraire::schedule::Date d{.year = 2025, .month = 6, .day = 15};
    const numeraire::products::VanillaEquityOptionProduct opt(
            "SPX", numeraire::OptionType::kCall, numeraire::ExerciseStyle::kEuropean, 100.0, d, d);

    const numeraire::pricers::AnalyticBlackScholesEquityPricer pricer;
    const numeraire::core::PricingResult out = pricer.Price(opt, m);
    ASSERT_TRUE(out.Npv().has_value());
    EXPECT_DOUBLE_EQ(*out.Npv(), 5.0);
}

TEST(AnalyticBlackScholesEquityPricerTest, AmericanExerciseThrows) {
    MapMarket m;
    m.SetSpot("SPX", 100.0);
    m.SetRate(0.05);
    m.SetVol(0.2);

    const numeraire::products::VanillaEquityOptionProduct opt(
            "SPX", numeraire::OptionType::kCall, numeraire::ExerciseStyle::kAmerican, 100.0,
            numeraire::schedule::Date{.year = 2025, .month = 1, .day = 1},
            numeraire::schedule::Date{.year = 2026, .month = 1, .day = 1});

    const numeraire::pricers::AnalyticBlackScholesEquityPricer pricer;
    EXPECT_THROW(static_cast<void>(pricer.Price(opt, m)), numeraire::ValidationError);
}

TEST(AnalyticBlackScholesEquityPricerTest, WrongProductTypeThrows) {
    MapMarket m;
    m.SetSpot("X", 100.0);
    const WrongProduct wrong;
    const numeraire::pricers::AnalyticBlackScholesEquityPricer pricer;
    EXPECT_THROW(static_cast<void>(pricer.Price(wrong, m)), numeraire::ValidationError);
}

TEST(AnalyticBlackScholesEquityPricerTest, EngineKindIsAnalytic) {
    const numeraire::pricers::AnalyticBlackScholesEquityPricer pricer;
    EXPECT_EQ(pricer.EngineKind(), numeraire::PricingEngineType::kAnalytic);
}
