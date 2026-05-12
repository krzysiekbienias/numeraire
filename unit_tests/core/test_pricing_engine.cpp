#include <gtest/gtest.h>

#include <numeraire/core/imarket_data.hpp>
#include <numeraire/core/ipricer.hpp>
#include <numeraire/core/iproduct.hpp>
#include <numeraire/core/pricing_engine.hpp>
#include <numeraire/enums/exercise_style.hpp>
#include <numeraire/enums/option_type.hpp>

#include <memory>
#include <string>
#include <unordered_map>

namespace {

class MapBackedMarketData final : public numeraire::core::IMarketData {
   public:
    [[nodiscard]] double Spot(const std::string_view underlying_id) const override {
        return spots_.at(std::string(underlying_id));
    }

    [[nodiscard]] double RiskFreeRate() const override { return 0.03; }

    [[nodiscard]] double DividendYield(const std::string_view underlying_id) const override {
        static_cast<void>(underlying_id);
        return 0.0;
    }

    [[nodiscard]] double ImpliedVolatility(const std::string_view underlying_id, const double strike,
                                           const double time_to_expiry_years) const override {
        static_cast<void>(underlying_id);
        static_cast<void>(strike);
        static_cast<void>(time_to_expiry_years);
        return 0.20;
    }

    void SetSpot(std::string id, const double v) { spots_[std::move(id)] = v; }

   private:
    std::unordered_map<std::string, double> spots_;
};

class VanillaOptionProduct final : public numeraire::core::IProduct {
   public:
    VanillaOptionProduct(std::string underlying, const double strike,
                         numeraire::schedule::Date trade, numeraire::schedule::Date expiry)
        : underlying_(std::move(underlying)), strike_(strike), trade_(trade), expiry_(expiry) {}

    [[nodiscard]] std::string_view UnderlyingId() const override { return underlying_; }

    [[nodiscard]] numeraire::OptionType OptionKind() const override {
        return numeraire::OptionType::kCall;
    }

    [[nodiscard]] numeraire::ExerciseStyle Exercise() const override {
        return numeraire::ExerciseStyle::kEuropean;
    }

    [[nodiscard]] double Strike() const override { return strike_; }

    [[nodiscard]] const numeraire::schedule::Date& TradeDate() const override { return trade_; }

    [[nodiscard]] const numeraire::schedule::Date& ExpiryDate() const override { return expiry_; }

    [[nodiscard]] const numeraire::schedule::Schedule* PaymentSchedule() const override {
        return nullptr;
    }

   private:
    std::string underlying_;
    double strike_;
    numeraire::schedule::Date trade_;
    numeraire::schedule::Date expiry_;
};

class IntrinsicCallPricer final : public numeraire::core::IPricer {
   public:
    [[nodiscard]] numeraire::PricingEngineType EngineKind() const override {
        return numeraire::PricingEngineType::kAnalytic;
    }

    [[nodiscard]] numeraire::core::PricingResult Price(const numeraire::core::IProduct& product,
                                                        const numeraire::core::IMarketData& market)
        const override {
        const double spot = market.Spot(product.UnderlyingId());
        const double strike = product.Strike();
        numeraire::core::PricingResult out;
        out.SetNpv(spot > strike ? spot - strike : 0.0);
        return out;
    }
};

}  // namespace

TEST(PricingEngineTest, ForwardsToPricerSameAsDirectCall) {
    MapBackedMarketData mkt;
    mkt.SetSpot("AAA", 102.0);

    const VanillaOptionProduct opt("AAA", 100.0,
                                   numeraire::schedule::Date{.year = 2025, .month = 1, .day = 1},
                                   numeraire::schedule::Date{.year = 2025, .month = 12, .day = 31});

    const IntrinsicCallPricer pricer;

    const numeraire::core::PricingResult direct = pricer.Price(opt, mkt);
    const numeraire::core::PricingResult via_engine =
            numeraire::core::PricingEngine::Price(opt, pricer, mkt);

    ASSERT_TRUE(direct.Npv().has_value());
    ASSERT_TRUE(via_engine.Npv().has_value());
    EXPECT_DOUBLE_EQ(*direct.Npv(), *via_engine.Npv());
    EXPECT_DOUBLE_EQ(*via_engine.Npv(), 2.0);
}

TEST(PricingEngineTest, WorksWithPolymorphicPricerReference) {
    MapBackedMarketData mkt;
    mkt.SetSpot("AAA", 95.0);

    const VanillaOptionProduct opt("AAA", 100.0,
                                   numeraire::schedule::Date{.year = 2025, .month = 1, .day = 1},
                                   numeraire::schedule::Date{.year = 2025, .month = 12, .day = 31});

    const auto pricer = std::make_unique<IntrinsicCallPricer>();
    const numeraire::core::PricingResult r =
            numeraire::core::PricingEngine::Price(opt, *pricer, mkt);

    ASSERT_TRUE(r.Npv().has_value());
    EXPECT_DOUBLE_EQ(*r.Npv(), 0.0);
}
