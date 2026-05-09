#include <gtest/gtest.h>

#include <numeraire/schedule/date.hpp>
#include <numeraire/schedule/schedule_generator.hpp>
#include <numeraire/schedule/schedule_quantlib.hpp>
#include <numeraire/utils/exception.hpp>
#include <numeraire/utils/quantlib_bridge.hpp>

#include <ql/time/schedule.hpp>

using numeraire::CalendarType;
using numeraire::DateConvention;
using numeraire::DateGenerationRule;
using numeraire::DayCount;
using numeraire::Frequency;
using numeraire::schedule::Date;
using numeraire::schedule::ScheduleGenerator;
using numeraire::schedule::ScheduleToQuantLib;
using numeraire::schedule::ToQuantLibDate;
using numeraire::utils::quantlib_bridge::ToQuantLib;

TEST(ScheduleGeneratorTest, MatchesRawQuantLibDefaultConfig) {
    const Date start{.year = 2025, .month = 1, .day = 15};
    const Date end{.year = 2025, .month = 12, .day = 15};
    const ScheduleGenerator gen = ScheduleGenerator::NewBuilder().Build();
    const auto ours = gen.Generate(start, end);

    const QuantLib::Date q_start = ToQuantLibDate(start);
    const QuantLib::Date q_end = ToQuantLibDate(end);
    const QuantLib::Period tenor(ToQuantLib(Frequency::kMonthly));
    const QuantLib::Schedule reference(q_start,
                                       q_end,
                                       tenor,
                                       ToQuantLib(CalendarType::kUnitedKingdom),
                                       ToQuantLib(DateConvention::kFollowing),
                                       ToQuantLib(DateConvention::kFollowing),
                                       ToQuantLib(DateGenerationRule::kForward),
                                       false,
                                       QuantLib::Date(),
                                       QuantLib::Date());

    ASSERT_EQ(ours.dates.size(), reference.size());
    for (std::size_t i = 0; i < ours.dates.size(); ++i) {
        EXPECT_EQ(ToQuantLibDate(ours.dates[i]), reference.date(i));
    }
}

TEST(ScheduleGeneratorTest, ScheduleToQuantLibWrapsSameDates) {
    const Date start{.year = 2026, .month = 3, .day = 1};
    const Date end{.year = 2026, .month = 9, .day = 30};
    const ScheduleGenerator gen = ScheduleGenerator::NewBuilder()
                                          .WithCalendar(CalendarType::kTarget)
                                          .WithFrequency(Frequency::kQuarterly)
                                          .Build();
    const auto schedule = gen.Generate(start, end);
    const QuantLib::Schedule wrapped = ScheduleToQuantLib(schedule, gen.Config());

    ASSERT_EQ(wrapped.size(), schedule.dates.size());
    for (std::size_t i = 0; i < schedule.dates.size(); ++i) {
        EXPECT_EQ(wrapped.date(i), ToQuantLibDate(schedule.dates[i]));
    }
}

TEST(ScheduleGeneratorTest, YearFractionMatchesDayCounter) {
    const Date d0{.year = 2025, .month = 1, .day = 1};
    const Date d1{.year = 2025, .month = 7, .day = 1};
    const ScheduleGenerator gen = ScheduleGenerator::NewBuilder().WithDayCount(DayCount::kActual365Fixed).Build();
    const double ours = gen.YearFraction(d0, d1);
    const double ref = ToQuantLib(DayCount::kActual365Fixed).yearFraction(ToQuantLibDate(d0), ToQuantLibDate(d1));
    EXPECT_DOUBLE_EQ(ours, ref);
}

TEST(ScheduleGeneratorTest, IsBusinessDayWeekend) {
    const ScheduleGenerator gen = ScheduleGenerator::NewBuilder().WithCalendar(CalendarType::kUnitedKingdom).Build();
    EXPECT_FALSE(gen.IsBusinessDay(Date{2025, 1, 4}));  // Saturday
    EXPECT_FALSE(gen.IsBusinessDay(Date{2025, 1, 5}));  // Sunday
    EXPECT_TRUE(gen.IsBusinessDay(Date{2025, 1, 6}));   // Monday
}

TEST(ScheduleGeneratorTest, AdjustFollowingSkipsWeekend) {
    const ScheduleGenerator gen = ScheduleGenerator::NewBuilder()
                                          .WithCalendar(CalendarType::kUnitedKingdom)
                                          .WithConvention(DateConvention::kFollowing)
                                          .Build();
    const Date adjusted = gen.Adjust(Date{2025, 1, 4});  // Saturday
    EXPECT_EQ(adjusted.year, 2025);
    EXPECT_EQ(adjusted.month, 1);
    EXPECT_EQ(adjusted.day, 6);  // Monday
}

TEST(ScheduleGeneratorTest, StartAfterEndThrows) {
    const ScheduleGenerator gen = ScheduleGenerator::NewBuilder().Build();
    EXPECT_THROW((void)gen.Generate(Date{2025, 12, 31}, Date{2025, 1, 1}), numeraire::ScheduleError);
}
