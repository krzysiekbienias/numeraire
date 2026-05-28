#include <gtest/gtest.h>

#include <cmath>
#include <numeraire/core/imarket_data.hpp>
#include <numeraire/core/iproduct.hpp>
#include <numeraire/enums/exercise_style.hpp>
#include <numeraire/enums/option_type.hpp>
#include <numeraire/pricers/analytic_black_scholes_equity_pricer.hpp>
#include <numeraire/products/equity_asset_or_nothing_product.hpp>
#include <numeraire/products/equity_cash_or_nothing_product.hpp>
#include <numeraire/products/equity_forward_product.hpp>
#include <numeraire/products/vanilla_equity_option_product.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/utils/exception.hpp>
#include <numeraire/utils/quantlib_bridge.hpp>
#include <string>
#include <unordered_map>

#include <ql/pricingengines/blackcalculator.hpp>

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

class WrongProduct final : public numeraire::core::IProduct {
public:
    [[nodiscard]] std::string_view UnderlyingId() const override { return "X"; }

    [[nodiscard]] numeraire::OptionType OptionKind() const override { return numeraire::OptionType::kCall; }

    [[nodiscard]] numeraire::ExerciseStyle Exercise() const override { return numeraire::ExerciseStyle::kEuropean; }

    [[nodiscard]] double Strike() const override { return 100.0; }

    [[nodiscard]] const numeraire::schedule::Date& TradeDate() const override { return d_; }

    [[nodiscard]] const numeraire::schedule::Date& ExpiryDate() const override { return d_; }

    [[nodiscard]] const numeraire::schedule::Schedule* PaymentSchedule() const override { return nullptr; }

private:
    numeraire::schedule::Date d_{.year = 2025, .month = 6, .day = 1};
};

/// Benchmark NPV / greeks via QuantLib Black-76 on forward (equivalent to BS on spot).
[[nodiscard]] QuantLib::BlackCalculator BenchBlack(const numeraire::OptionType kind,
                                                   const double strike,
                                                   const double spot,
                                                   const double r,
                                                   const double q,
                                                   const double vol,
                                                   const double tau) {
    const double forward = spot * std::exp((r - q) * tau);
    const double std_dev = vol * std::sqrt(tau);
    const double discount = std::exp(-r * tau);
    return QuantLib::BlackCalculator(
            numeraire::utils::quantlib_bridge::ToQuantLib(kind), strike, forward, std_dev, discount);
}

[[nodiscard]] double BenchAssetOrNothingNpv(const numeraire::OptionType kind,
                                            const double spot,
                                            const double strike,
                                            const double r,
                                            const double q,
                                            const double vol,
                                            const double tau) {
    const double srt = vol * std::sqrt(tau);
    const double d1 = (std::log(spot / strike) + ((r - q) + 0.5 * vol * vol) * tau) / srt;
    const double eqt = std::exp(-q * tau);
    if (kind == numeraire::OptionType::kCall) {
        return spot * eqt * 0.5 * (1.0 + std::erf(d1 / std::sqrt(2.0)));
    }
    return spot * eqt * 0.5 * (1.0 + std::erf(-d1 / std::sqrt(2.0)));
}

[[nodiscard]] double BenchCashOrNothingNpv(const numeraire::OptionType kind,
                                           const double spot,
                                           const double strike,
                                           const double cash_payout,
                                           const double r,
                                           const double q,
                                           const double vol,
                                           const double tau) {
    const double srt = vol * std::sqrt(tau);
    const double d1 = (std::log(spot / strike) + ((r - q) + 0.5 * vol * vol) * tau) / srt;
    const double d2 = d1 - srt;
    const double discount = std::exp(-r * tau);
    if (kind == numeraire::OptionType::kCall) {
        return cash_payout * discount * 0.5 * (1.0 + std::erf(d2 / std::sqrt(2.0)));
    }
    return cash_payout * discount * 0.5 * (1.0 + std::erf(-d2 / std::sqrt(2.0)));
}

}  // namespace

TEST(AnalyticBlackScholesEquityPricerTest, AssetOrNothingCallMatchesClosedForm) {
    const numeraire::schedule::Date trade{.year = 2025, .month = 1, .day = 1};
    const numeraire::schedule::Date expiry{.year = 2026, .month = 1, .day = 1};
    const double tau = numeraire::schedule::Act365FixedYearFraction(trade, expiry);

    MapMarket m;
    m.SetValuationDate(trade);
    m.SetSpot("AAPL", 100.0);
    m.SetRate(0.05);
    m.SetDivYield(0.02);
    m.SetVol(0.25);

    const numeraire::products::EquityAssetOrNothingProduct opt(
            "AAPL", numeraire::OptionType::kCall, numeraire::ExerciseStyle::kEuropean, 100.0, trade, expiry);

    const numeraire::pricers::AnalyticBlackScholesEquityPricer pricer;
    const numeraire::core::PricingResult out = pricer.Price(opt, m);

    const double expected = BenchAssetOrNothingNpv(numeraire::OptionType::kCall, 100.0, 100.0, 0.05, 0.02, 0.25, tau);
    ASSERT_TRUE(out.Npv().has_value());
    EXPECT_NEAR(*out.Npv(), expected, 1e-12);
    EXPECT_FALSE(out.Greeks().has_value());
}

TEST(AnalyticBlackScholesEquityPricerTest, AssetOrNothingPutMatchesClosedForm) {
    const numeraire::schedule::Date trade{.year = 2025, .month = 1, .day = 1};
    const numeraire::schedule::Date expiry{.year = 2026, .month = 1, .day = 1};
    const double tau = numeraire::schedule::Act365FixedYearFraction(trade, expiry);

    MapMarket m;
    m.SetValuationDate(trade);
    m.SetSpot("GOOGL", 100.0);
    m.SetRate(0.03);
    m.SetDivYield(0.0);
    m.SetVol(0.20);

    const numeraire::products::EquityAssetOrNothingProduct opt(
            "GOOGL", numeraire::OptionType::kPut, numeraire::ExerciseStyle::kEuropean, 110.0, trade, expiry);

    const numeraire::pricers::AnalyticBlackScholesEquityPricer pricer;
    const numeraire::core::PricingResult out = pricer.Price(opt, m);

    const double expected = BenchAssetOrNothingNpv(numeraire::OptionType::kPut, 100.0, 110.0, 0.03, 0.0, 0.20, tau);
    ASSERT_TRUE(out.Npv().has_value());
    EXPECT_NEAR(*out.Npv(), expected, 1e-12);
}

TEST(AnalyticBlackScholesEquityPricerTest, AssetOrNothingZeroTimeIsSpotIfItm) {
    const numeraire::schedule::Date d{.year = 2025, .month = 6, .day = 15};

    MapMarket m;
    m.SetValuationDate(d);
    m.SetSpot("AAPL", 105.0);
    m.SetRate(0.05);
    m.SetVol(0.2);

    const numeraire::products::EquityAssetOrNothingProduct call(
            "AAPL", numeraire::OptionType::kCall, numeraire::ExerciseStyle::kEuropean, 100.0, d, d);
    const numeraire::products::EquityAssetOrNothingProduct put(
            "AAPL", numeraire::OptionType::kPut, numeraire::ExerciseStyle::kEuropean, 110.0, d, d);

    const numeraire::pricers::AnalyticBlackScholesEquityPricer pricer;
    const numeraire::core::PricingResult call_out = pricer.Price(call, m);
    const numeraire::core::PricingResult put_out = pricer.Price(put, m);

    ASSERT_TRUE(call_out.Npv().has_value());
    EXPECT_DOUBLE_EQ(*call_out.Npv(), 105.0);
    ASSERT_TRUE(put_out.Npv().has_value());
    EXPECT_DOUBLE_EQ(*put_out.Npv(), 105.0);
}

TEST(AnalyticBlackScholesEquityPricerTest, AssetOrNothingZeroTimeOtmIsZero) {
    const numeraire::schedule::Date d{.year = 2025, .month = 6, .day = 15};

    MapMarket m;
    m.SetValuationDate(d);
    m.SetSpot("AAPL", 95.0);
    m.SetRate(0.05);
    m.SetVol(0.2);

    const numeraire::products::EquityAssetOrNothingProduct call(
            "AAPL", numeraire::OptionType::kCall, numeraire::ExerciseStyle::kEuropean, 100.0, d, d);

    const numeraire::pricers::AnalyticBlackScholesEquityPricer pricer;
    const numeraire::core::PricingResult out = pricer.Price(call, m);

    ASSERT_TRUE(out.Npv().has_value());
    EXPECT_DOUBLE_EQ(*out.Npv(), 0.0);
}

TEST(AnalyticBlackScholesEquityPricerTest, CashOrNothingCallMatchesClosedForm) {
    const numeraire::schedule::Date trade{.year = 2025, .month = 1, .day = 1};
    const numeraire::schedule::Date expiry{.year = 2026, .month = 1, .day = 1};
    const double tau = numeraire::schedule::Act365FixedYearFraction(trade, expiry);

    MapMarket m;
    m.SetValuationDate(trade);
    m.SetSpot("NVDA", 250.0);
    m.SetRate(0.03);
    m.SetDivYield(0.0);
    m.SetVol(0.35);

    const numeraire::products::EquityCashOrNothingProduct opt(
            "NVDA", numeraire::OptionType::kCall, numeraire::ExerciseStyle::kEuropean, 220.0, 1.0, trade,
            expiry);

    const numeraire::pricers::AnalyticBlackScholesEquityPricer pricer;
    const numeraire::core::PricingResult out = pricer.Price(opt, m);

    const double expected = BenchCashOrNothingNpv(
            numeraire::OptionType::kCall, 250.0, 220.0, 1.0, 0.03, 0.0, 0.35, tau);
    ASSERT_TRUE(out.Npv().has_value());
    EXPECT_NEAR(*out.Npv(), expected, 1e-12);
    EXPECT_FALSE(out.Greeks().has_value());
}

TEST(AnalyticBlackScholesEquityPricerTest, CashOrNothingPutMatchesClosedForm) {
    const numeraire::schedule::Date trade{.year = 2025, .month = 1, .day = 1};
    const numeraire::schedule::Date expiry{.year = 2026, .month = 1, .day = 1};
    const double tau = numeraire::schedule::Act365FixedYearFraction(trade, expiry);

    MapMarket m;
    m.SetValuationDate(trade);
    m.SetSpot("AAPL", 280.0);
    m.SetRate(0.03);
    m.SetDivYield(0.0);
    m.SetVol(0.22);

    const numeraire::products::EquityCashOrNothingProduct opt(
            "AAPL", numeraire::OptionType::kPut, numeraire::ExerciseStyle::kEuropean, 295.0, 1.0, trade,
            expiry);

    const numeraire::pricers::AnalyticBlackScholesEquityPricer pricer;
    const numeraire::core::PricingResult out = pricer.Price(opt, m);

    const double expected = BenchCashOrNothingNpv(
            numeraire::OptionType::kPut, 280.0, 295.0, 1.0, 0.03, 0.0, 0.22, tau);
    ASSERT_TRUE(out.Npv().has_value());
    EXPECT_NEAR(*out.Npv(), expected, 1e-12);
}

TEST(AnalyticBlackScholesEquityPricerTest, CashOrNothingZeroTimeIsPayoutIfItm) {
    const numeraire::schedule::Date d{.year = 2025, .month = 6, .day = 15};
    constexpr double kPayout = 1.0;

    MapMarket m;
    m.SetValuationDate(d);
    m.SetSpot("AAPL", 300.0);
    m.SetRate(0.05);
    m.SetVol(0.2);

    const numeraire::products::EquityCashOrNothingProduct call(
            "AAPL", numeraire::OptionType::kCall, numeraire::ExerciseStyle::kEuropean, 295.0, kPayout, d, d);
    const numeraire::products::EquityCashOrNothingProduct put(
            "AAPL", numeraire::OptionType::kPut, numeraire::ExerciseStyle::kEuropean, 305.0, kPayout, d, d);

    const numeraire::pricers::AnalyticBlackScholesEquityPricer pricer;
    const numeraire::core::PricingResult call_out = pricer.Price(call, m);
    const numeraire::core::PricingResult put_out = pricer.Price(put, m);

    ASSERT_TRUE(call_out.Npv().has_value());
    EXPECT_DOUBLE_EQ(*call_out.Npv(), kPayout);
    ASSERT_TRUE(put_out.Npv().has_value());
    EXPECT_DOUBLE_EQ(*put_out.Npv(), kPayout);
}

TEST(AnalyticBlackScholesEquityPricerTest, CashOrNothingZeroTimeOtmIsZero) {
    const numeraire::schedule::Date d{.year = 2025, .month = 6, .day = 15};

    MapMarket m;
    m.SetValuationDate(d);
    m.SetSpot("AAPL", 290.0);
    m.SetRate(0.05);
    m.SetVol(0.2);

    const numeraire::products::EquityCashOrNothingProduct call(
            "AAPL", numeraire::OptionType::kCall, numeraire::ExerciseStyle::kEuropean, 295.0, 1.0, d, d);

    const numeraire::pricers::AnalyticBlackScholesEquityPricer pricer;
    const numeraire::core::PricingResult out = pricer.Price(call, m);

    ASSERT_TRUE(out.Npv().has_value());
    EXPECT_DOUBLE_EQ(*out.Npv(), 0.0);
}

TEST(AnalyticBlackScholesEquityPricerTest, RejectsForwardProduct) {
    const numeraire::schedule::Date d{.year = 2025, .month = 6, .day = 15};

    MapMarket m;
    m.SetValuationDate(d);
    m.SetSpot("AAPL", 100.0);

    const numeraire::products::EquityForwardProduct forward("AAPL", 95.0, d, d);
    const numeraire::pricers::AnalyticBlackScholesEquityPricer pricer;
    EXPECT_THROW(static_cast<void>(pricer.Price(forward, m)), numeraire::ValidationError);
}

TEST(AnalyticBlackScholesEquityPricerTest, EuropeanCallMatchesQuantLibBenchmark) {
    const numeraire::schedule::Date trade{.year = 2025, .month = 1, .day = 1};
    const numeraire::schedule::Date expiry{.year = 2026, .month = 1, .day = 1};
    const double tau = numeraire::schedule::Act365FixedYearFraction(trade, expiry);

    MapMarket m;
    m.SetValuationDate(trade);
    m.SetSpot("SPX", 100.0);
    m.SetRate(0.05);
    m.SetDivYield(0.02);
    m.SetVol(0.25);

    const numeraire::products::VanillaEquityOptionProduct opt(
            "SPX", numeraire::OptionType::kCall, numeraire::ExerciseStyle::kEuropean, 100.0, trade, expiry);

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
    const numeraire::schedule::Date trade{.year = 2025, .month = 1, .day = 1};
    const numeraire::schedule::Date expiry{.year = 2026, .month = 1, .day = 1};
    const double tau = numeraire::schedule::Act365FixedYearFraction(trade, expiry);

    MapMarket m;
    m.SetValuationDate(trade);
    m.SetSpot("SPX", 100.0);
    m.SetRate(0.05);
    m.SetDivYield(0.02);
    m.SetVol(0.25);

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
    const numeraire::schedule::Date d{.year = 2025, .month = 6, .day = 15};

    MapMarket m;
    m.SetValuationDate(d);
    m.SetSpot("SPX", 105.0);
    m.SetRate(0.05);
    m.SetVol(0.2);
    const numeraire::products::VanillaEquityOptionProduct opt(
            "SPX", numeraire::OptionType::kCall, numeraire::ExerciseStyle::kEuropean, 100.0, d, d);

    const numeraire::pricers::AnalyticBlackScholesEquityPricer pricer;
    const numeraire::core::PricingResult out = pricer.Price(opt, m);
    ASSERT_TRUE(out.Npv().has_value());
    EXPECT_DOUBLE_EQ(*out.Npv(), 5.0);
}

TEST(AnalyticBlackScholesEquityPricerTest, AmericanExerciseThrows) {
    MapMarket m;
    m.SetValuationDate(numeraire::schedule::Date{.year = 2025, .month = 1, .day = 1});
    m.SetSpot("SPX", 100.0);
    m.SetRate(0.05);
    m.SetVol(0.2);

    const numeraire::products::VanillaEquityOptionProduct opt(
            "SPX",
            numeraire::OptionType::kCall,
            numeraire::ExerciseStyle::kAmerican,
            100.0,
            numeraire::schedule::Date{.year = 2025, .month = 1, .day = 1},
            numeraire::schedule::Date{.year = 2026, .month = 1, .day = 1});

    const numeraire::pricers::AnalyticBlackScholesEquityPricer pricer;
    EXPECT_THROW(static_cast<void>(pricer.Price(opt, m)), numeraire::ValidationError);
}

TEST(AnalyticBlackScholesEquityPricerTest, TimeToExpiryUsesValuationDateNotTradeDate) {
    const numeraire::schedule::Date trade{.year = 2024, .month = 1, .day = 1};
    const numeraire::schedule::Date valuation{.year = 2025, .month = 1, .day = 1};
    const numeraire::schedule::Date expiry{.year = 2026, .month = 1, .day = 1};

    MapMarket m;
    m.SetValuationDate(valuation);
    m.SetSpot("SPX", 100.0);
    m.SetRate(0.05);
    m.SetDivYield(0.0);
    m.SetVol(0.25);

    const numeraire::products::VanillaEquityOptionProduct opt(
            "SPX", numeraire::OptionType::kCall, numeraire::ExerciseStyle::kEuropean, 100.0, trade, expiry);

    const numeraire::pricers::AnalyticBlackScholesEquityPricer pricer;
    const numeraire::core::PricingResult out = pricer.Price(opt, m);

    const double tau_valuation = numeraire::schedule::Act365FixedYearFraction(valuation, expiry);
    const QuantLib::BlackCalculator bench =
            BenchBlack(numeraire::OptionType::kCall, 100.0, 100.0, 0.05, 0.0, 0.25, tau_valuation);

    ASSERT_TRUE(out.Npv().has_value());
    EXPECT_NEAR(*out.Npv(), bench.value(), 1e-12);
    EXPECT_LT(tau_valuation, numeraire::schedule::Act365FixedYearFraction(trade, expiry));
}

TEST(AnalyticBlackScholesEquityPricerTest, WrongProductTypeThrows) {
    MapMarket m;
    m.SetValuationDate(numeraire::schedule::Date{.year = 2025, .month = 6, .day = 1});
    m.SetSpot("X", 100.0);
    const WrongProduct wrong;
    const numeraire::pricers::AnalyticBlackScholesEquityPricer pricer;
    EXPECT_THROW(static_cast<void>(pricer.Price(wrong, m)), numeraire::ValidationError);
}

TEST(AnalyticBlackScholesEquityPricerTest, EngineKindIsAnalytic) {
    const numeraire::pricers::AnalyticBlackScholesEquityPricer pricer;
    EXPECT_EQ(pricer.EngineKind(), numeraire::PricingEngineType::kAnalytic);
}
