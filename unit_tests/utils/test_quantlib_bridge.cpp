#include <gtest/gtest.h>

#include <numeraire/utils/quantlib_bridge.hpp>

using numeraire::CalendarType;
using numeraire::Currency;
using numeraire::DateConvention;
using numeraire::DateGenerationRule;
using numeraire::DayCount;
using numeraire::ExerciseStyle;
using numeraire::Frequency;
using numeraire::OptionType;
using numeraire::utils::quantlib_bridge::FromQuantLib;
using numeraire::utils::quantlib_bridge::ToQuantLib;

TEST(QuantLibBridgeTest, OptionTypeRoundTrip) {
    EXPECT_EQ(FromQuantLib(ToQuantLib(OptionType::kCall)), OptionType::kCall);
    EXPECT_EQ(FromQuantLib(ToQuantLib(OptionType::kPut)), OptionType::kPut);
}

TEST(QuantLibBridgeTest, ExerciseStyleRoundTrip) {
    EXPECT_EQ(FromQuantLib(ToQuantLib(ExerciseStyle::kEuropean)), ExerciseStyle::kEuropean);
    EXPECT_EQ(FromQuantLib(ToQuantLib(ExerciseStyle::kAmerican)), ExerciseStyle::kAmerican);
    EXPECT_EQ(FromQuantLib(ToQuantLib(ExerciseStyle::kBermudan)), ExerciseStyle::kBermudan);
}

TEST(QuantLibBridgeTest, FrequencyRoundTrip) {
    EXPECT_EQ(FromQuantLib(ToQuantLib(Frequency::kMonthly)), Frequency::kMonthly);
    EXPECT_EQ(FromQuantLib(ToQuantLib(Frequency::kQuarterly)), Frequency::kQuarterly);
    EXPECT_EQ(FromQuantLib(ToQuantLib(Frequency::kAnnual)), Frequency::kAnnual);
}

TEST(QuantLibBridgeTest, DateConventionRoundTrip) {
    EXPECT_EQ(FromQuantLib(ToQuantLib(DateConvention::kFollowing)), DateConvention::kFollowing);
    EXPECT_EQ(FromQuantLib(ToQuantLib(DateConvention::kModifiedFollowing)), DateConvention::kModifiedFollowing);
}

TEST(QuantLibBridgeTest, DateGenerationRuleRoundTrip) {
    EXPECT_EQ(FromQuantLib(ToQuantLib(DateGenerationRule::kForward)), DateGenerationRule::kForward);
    EXPECT_EQ(FromQuantLib(ToQuantLib(DateGenerationRule::kBackward)), DateGenerationRule::kBackward);
}

TEST(QuantLibBridgeTest, DayCountRoundTrip) {
    EXPECT_EQ(FromQuantLib(ToQuantLib(DayCount::kActual365Fixed)), DayCount::kActual365Fixed);
    EXPECT_EQ(FromQuantLib(ToQuantLib(DayCount::kActual360)), DayCount::kActual360);
    EXPECT_EQ(FromQuantLib(ToQuantLib(DayCount::kThirty360)), DayCount::kThirty360);
}

TEST(QuantLibBridgeTest, CurrencyRoundTrip) {
    EXPECT_EQ(FromQuantLib(ToQuantLib(Currency::kUsd)), Currency::kUsd);
    EXPECT_EQ(FromQuantLib(ToQuantLib(Currency::kEur)), Currency::kEur);
}

TEST(QuantLibBridgeTest, CalendarUnitedKingdomRoundTrip) {
    const QuantLib::Calendar ql = ToQuantLib(CalendarType::kUnitedKingdom);
    EXPECT_EQ(FromQuantLib(ql), CalendarType::kUnitedKingdom);
}

TEST(QuantLibBridgeTest, CalendarMatchesDefaultJsonSemantics) {
    // configs/default.json schedule: UnitedKingdom, Monthly, Following, Forward, Actual365Fixed
    EXPECT_EQ(FromQuantLib(ToQuantLib(CalendarType::kUnitedKingdom)), CalendarType::kUnitedKingdom);
    EXPECT_EQ(FromQuantLib(ToQuantLib(Frequency::kMonthly)), Frequency::kMonthly);
    EXPECT_EQ(FromQuantLib(ToQuantLib(DateConvention::kFollowing)), DateConvention::kFollowing);
    EXPECT_EQ(FromQuantLib(ToQuantLib(DateGenerationRule::kForward)), DateGenerationRule::kForward);
    EXPECT_EQ(FromQuantLib(ToQuantLib(DayCount::kActual365Fixed)), DayCount::kActual365Fixed);
    EXPECT_EQ(FromQuantLib(ToQuantLib(Currency::kUsd)), Currency::kUsd);
}
