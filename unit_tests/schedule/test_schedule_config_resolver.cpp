#include <gtest/gtest.h>

#include <numeraire/enums/calendar_type.hpp>
#include <numeraire/enums/date_convention.hpp>
#include <numeraire/enums/date_generation_rule.hpp>
#include <numeraire/enums/day_count.hpp>
#include <numeraire/enums/frequency.hpp>
#include <numeraire/schedule/schedule_config_resolver.hpp>
#include <numeraire/utils/config.hpp>
#include <numeraire/utils/exception.hpp>

#include <filesystem>

#ifndef NUMERAIRE_SOURCE_DIR
#    error "NUMERAIRE_SOURCE_DIR must be set by CMake (see unit_tests/CMakeLists.txt)."
#endif

namespace {

[[nodiscard]] std::filesystem::path DefaultCommittedConfigPath() {
    return std::filesystem::path(NUMERAIRE_SOURCE_DIR) / "configs/default.json";
}

[[nodiscard]] numeraire::utils::Config LoadDefaultConfig() {
    return numeraire::utils::Config::Load(DefaultCommittedConfigPath());
}

}  // namespace

TEST(ScheduleConfigResolverTest, EmptyOverridesMatchesDefaultJson) {
    const auto cfg = LoadDefaultConfig();
    const numeraire::schedule::TradeScheduleOverrides overrides{};
    const auto resolved = numeraire::schedule::ScheduleConfigResolver::Resolve(overrides, cfg);

    EXPECT_EQ(resolved.calendar_type, numeraire::CalendarType::kUnitedKingdom);
    EXPECT_EQ(resolved.frequency, numeraire::Frequency::kMonthly);
    EXPECT_EQ(resolved.convention, numeraire::DateConvention::kFollowing);
    EXPECT_EQ(resolved.rule, numeraire::DateGenerationRule::kForward);
    EXPECT_EQ(resolved.day_count, numeraire::DayCount::kActual365Fixed);
}

TEST(ScheduleConfigResolverTest, OverrideCalendarLeavesRestFromJson) {
    const auto cfg = LoadDefaultConfig();
    numeraire::schedule::TradeScheduleOverrides overrides{};
    overrides.calendar_type = "UnitedStates";

    const auto resolved = numeraire::schedule::ScheduleConfigResolver::Resolve(overrides, cfg);

    EXPECT_EQ(resolved.calendar_type, numeraire::CalendarType::kUnitedStates);
    EXPECT_EQ(resolved.frequency, numeraire::Frequency::kMonthly);
    EXPECT_EQ(resolved.convention, numeraire::DateConvention::kFollowing);
    EXPECT_EQ(resolved.rule, numeraire::DateGenerationRule::kForward);
    EXPECT_EQ(resolved.day_count, numeraire::DayCount::kActual365Fixed);
}

TEST(ScheduleConfigResolverTest, WhitespaceOnlyOverrideFallsBackToJson) {
    const auto cfg = LoadDefaultConfig();
    numeraire::schedule::TradeScheduleOverrides overrides{};
    overrides.calendar_type = "   ";

    const auto resolved = numeraire::schedule::ScheduleConfigResolver::Resolve(overrides, cfg);
    EXPECT_EQ(resolved.calendar_type, numeraire::CalendarType::kUnitedKingdom);
}

TEST(ScheduleConfigResolverTest, BadOverrideThrowsScheduleError) {
    const auto cfg = LoadDefaultConfig();
    numeraire::schedule::TradeScheduleOverrides overrides{};
    overrides.calendar_type = "Narnia";

    EXPECT_THROW((void)numeraire::schedule::ScheduleConfigResolver::Resolve(overrides, cfg),
                 numeraire::ScheduleError);
}
