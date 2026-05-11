#include <gtest/gtest.h>

#include <numeraire/core/iproduct.hpp>
#include <numeraire/enums/exercise_style.hpp>
#include <numeraire/enums/option_type.hpp>

#include <memory>
#include <optional>
#include <string>

namespace {

/// Vanilla equity option with optional owned schedule (bullet when unset).
class VanillaOptionProduct final : public numeraire::core::IProduct {
   public:
    VanillaOptionProduct(std::string underlying, const numeraire::OptionType kind,
                         const numeraire::ExerciseStyle exercise, const double strike,
                         numeraire::schedule::Date trade, numeraire::schedule::Date expiry,
                         std::optional<numeraire::schedule::Schedule> payments = std::nullopt)
            : underlying_(std::move(underlying)),
              kind_(kind),
              exercise_(exercise),
              strike_(strike),
              trade_(trade),
              expiry_(expiry),
              payments_(std::move(payments)) {}

    [[nodiscard]] std::string_view UnderlyingId() const override { return underlying_; }

    [[nodiscard]] numeraire::OptionType OptionKind() const override { return kind_; }

    [[nodiscard]] numeraire::ExerciseStyle Exercise() const override { return exercise_; }

    [[nodiscard]] double Strike() const override { return strike_; }

    [[nodiscard]] const numeraire::schedule::Date& TradeDate() const override { return trade_; }

    [[nodiscard]] const numeraire::schedule::Date& ExpiryDate() const override { return expiry_; }

    [[nodiscard]] const numeraire::schedule::Schedule* PaymentSchedule() const override {
        return payments_ ? &(*payments_) : nullptr;
    }

   private:
    std::string underlying_;
    numeraire::OptionType kind_;
    numeraire::ExerciseStyle exercise_;
    double strike_;
    numeraire::schedule::Date trade_;
    numeraire::schedule::Date expiry_;
    std::optional<numeraire::schedule::Schedule> payments_;
};

void ExpectVanillaThroughInterface(const numeraire::core::IProduct& p) {
    EXPECT_EQ(p.UnderlyingId(), "AAPL");
    EXPECT_EQ(p.OptionKind(), numeraire::OptionType::kCall);
    EXPECT_EQ(p.Exercise(), numeraire::ExerciseStyle::kEuropean);
    EXPECT_DOUBLE_EQ(p.Strike(), 233.0);
    EXPECT_EQ(p.TradeDate().year, 2025);
    EXPECT_EQ(p.ExpiryDate().month, 11);
    EXPECT_EQ(p.PaymentSchedule(), nullptr);
}

}  // namespace

TEST(IProductTest, BulletVanillaExposesFieldsAndNullSchedule) {
    const VanillaOptionProduct opt("AAPL", numeraire::OptionType::kCall,
                                   numeraire::ExerciseStyle::kEuropean, 233.0,
                                   numeraire::schedule::Date{.year = 2025, .month = 8, .day = 4},
                                   numeraire::schedule::Date{.year = 2025, .month = 11, .day = 4});

    ExpectVanillaThroughInterface(opt);
}

TEST(IProductTest, PolymorphicCallThroughInterface) {
    const auto opt = std::make_unique<VanillaOptionProduct>(
            "AAPL", numeraire::OptionType::kCall, numeraire::ExerciseStyle::kEuropean, 233.0,
            numeraire::schedule::Date{.year = 2025, .month = 8, .day = 4},
            numeraire::schedule::Date{.year = 2025, .month = 11, .day = 4});

    const numeraire::core::IProduct& ref = *opt;
    ExpectVanillaThroughInterface(ref);
}

TEST(IProductTest, OptionalPaymentScheduleNonNull) {
    numeraire::schedule::Schedule payments;
    payments.dates.push_back(
            numeraire::schedule::Date{.year = 2025, .month = 8, .day = 4});
    payments.dates.push_back(
            numeraire::schedule::Date{.year = 2025, .month = 11, .day = 4});

    const VanillaOptionProduct opt("MSFT", numeraire::OptionType::kPut,
                                   numeraire::ExerciseStyle::kEuropean, 100.0,
                                   numeraire::schedule::Date{.year = 2025, .month = 1, .day = 1},
                                   numeraire::schedule::Date{.year = 2025, .month = 12, .day = 31},
                                   payments);

    ASSERT_NE(opt.PaymentSchedule(), nullptr);
    EXPECT_EQ(opt.PaymentSchedule()->Size(), 2U);
}
