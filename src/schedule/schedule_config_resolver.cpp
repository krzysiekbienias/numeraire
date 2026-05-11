#include <numeraire/schedule/schedule_config_resolver.hpp>

#include <numeraire/utils/config.hpp>
#include <numeraire/utils/exception.hpp>

#include <string>
#include <string_view>

namespace numeraire::schedule {
namespace {

constexpr std::string_view kDefaultCalendarPath = "schedule.default_calendar";
constexpr std::string_view kDefaultFrequencyPath = "schedule.default_frequency";
constexpr std::string_view kDefaultDayCountPath = "schedule.default_day_count";
constexpr std::string_view kDefaultConventionPath = "schedule.default_convention";
constexpr std::string_view kDefaultRulePath = "schedule.default_rule";

[[nodiscard]] std::string TrimCopy(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return std::string(s);
}

[[nodiscard]] bool HasOverride(const std::optional<std::string>& o) {
    return o.has_value() && !TrimCopy(*o).empty();
}

[[nodiscard]] std::string EffectiveCalendarString(const TradeScheduleOverrides& overrides,
                                                  const utils::Config& config) {
    if (HasOverride(overrides.calendar_type)) {
        return TrimCopy(*overrides.calendar_type);
    }
    return config.RequireString(kDefaultCalendarPath);
}

[[nodiscard]] std::string EffectiveFrequencyString(const TradeScheduleOverrides& overrides,
                                                   const utils::Config& config) {
    if (HasOverride(overrides.frequency)) {
        return TrimCopy(*overrides.frequency);
    }
    return config.RequireString(kDefaultFrequencyPath);
}

[[nodiscard]] std::string EffectiveConventionString(const TradeScheduleOverrides& overrides,
                                                    const utils::Config& config) {
    if (HasOverride(overrides.date_convention)) {
        return TrimCopy(*overrides.date_convention);
    }
    return config.RequireString(kDefaultConventionPath);
}

[[nodiscard]] std::string EffectiveRuleString(const TradeScheduleOverrides& overrides,
                                              const utils::Config& config) {
    if (HasOverride(overrides.generation_rule)) {
        return TrimCopy(*overrides.generation_rule);
    }
    return config.RequireString(kDefaultRulePath);
}

[[nodiscard]] std::string EffectiveDayCountString(const TradeScheduleOverrides& overrides,
                                                  const utils::Config& config) {
    if (HasOverride(overrides.day_count)) {
        return TrimCopy(*overrides.day_count);
    }
    return config.RequireString(kDefaultDayCountPath);
}

[[noreturn]] void ThrowParseError(std::string_view field_name, std::string_view value,
                                  std::string_view allowed_hint) {
    throw numeraire::ScheduleError(
            std::string("ScheduleConfigResolver: invalid ") + std::string(field_name) +
            " \"" + std::string(value) + "\". Expected " + std::string(allowed_hint) + ".");
}

[[nodiscard]] CalendarType ParseCalendarType(std::string_view value) {
    if (value == "UnitedKingdom") {
        return CalendarType::kUnitedKingdom;
    }
    if (value == "UnitedStates") {
        return CalendarType::kUnitedStates;
    }
    if (value == "Target") {
        return CalendarType::kTarget;
    }
    if (value == "Germany") {
        return CalendarType::kGermany;
    }
    ThrowParseError(
            "calendar_type", value,
            "UnitedKingdom, UnitedStates, Target, Germany (same as configs/default.json)");
}

[[nodiscard]] Frequency ParseFrequency(std::string_view value) {
    if (value == "NoFrequency") {
        return Frequency::kNoFrequency;
    }
    if (value == "Once") {
        return Frequency::kOnce;
    }
    if (value == "Annual") {
        return Frequency::kAnnual;
    }
    if (value == "Semiannual") {
        return Frequency::kSemiannual;
    }
    if (value == "EveryFourthMonth") {
        return Frequency::kEveryFourthMonth;
    }
    if (value == "Quarterly") {
        return Frequency::kQuarterly;
    }
    if (value == "Bimonthly") {
        return Frequency::kBimonthly;
    }
    if (value == "Monthly") {
        return Frequency::kMonthly;
    }
    if (value == "EveryFourthWeek") {
        return Frequency::kEveryFourthWeek;
    }
    if (value == "Biweekly") {
        return Frequency::kBiweekly;
    }
    if (value == "Weekly") {
        return Frequency::kWeekly;
    }
    if (value == "Daily") {
        return Frequency::kDaily;
    }
    if (value == "OtherFrequency") {
        return Frequency::kOtherFrequency;
    }
    ThrowParseError("frequency", value,
                    "Annual, Monthly, Quarterly, Weekly, Daily, … (PascalCase enum name without k)");
}

[[nodiscard]] DateConvention ParseDateConvention(std::string_view value) {
    if (value == "Following") {
        return DateConvention::kFollowing;
    }
    if (value == "ModifiedFollowing") {
        return DateConvention::kModifiedFollowing;
    }
    if (value == "Preceding") {
        return DateConvention::kPreceding;
    }
    if (value == "ModifiedPreceding") {
        return DateConvention::kModifiedPreceding;
    }
    if (value == "Unadjusted") {
        return DateConvention::kUnadjusted;
    }
    if (value == "HalfMonthModifiedFollowing") {
        return DateConvention::kHalfMonthModifiedFollowing;
    }
    if (value == "Nearest") {
        return DateConvention::kNearest;
    }
    ThrowParseError("date_convention", value,
                    "Following, ModifiedFollowing, Preceding, … (PascalCase names)");
}

[[nodiscard]] DateGenerationRule ParseDateGenerationRule(std::string_view value) {
    if (value == "Backward") {
        return DateGenerationRule::kBackward;
    }
    if (value == "Forward") {
        return DateGenerationRule::kForward;
    }
    if (value == "Zero") {
        return DateGenerationRule::kZero;
    }
    if (value == "ThirdWednesday") {
        return DateGenerationRule::kThirdWednesday;
    }
    if (value == "ThirdWednesdayInclusive") {
        return DateGenerationRule::kThirdWednesdayInclusive;
    }
    if (value == "Twentieth") {
        return DateGenerationRule::kTwentieth;
    }
    if (value == "TwentiethImm") {
        return DateGenerationRule::kTwentiethImm;
    }
    if (value == "OldCds") {
        return DateGenerationRule::kOldCds;
    }
    if (value == "Cds") {
        return DateGenerationRule::kCds;
    }
    if (value == "Cds2015") {
        return DateGenerationRule::kCds2015;
    }
    ThrowParseError("generation_rule", value, "Forward, Backward, Zero, … (PascalCase names)");
}

[[nodiscard]] DayCount ParseDayCount(std::string_view value) {
    if (value == "Actual360") {
        return DayCount::kActual360;
    }
    if (value == "Actual365Fixed") {
        return DayCount::kActual365Fixed;
    }
    if (value == "Thirty360") {
        return DayCount::kThirty360;
    }
    if (value == "ActualActualIsda") {
        return DayCount::kActualActualIsda;
    }
    if (value == "ActualActualBond") {
        return DayCount::kActualActualBond;
    }
    if (value == "Business252") {
        return DayCount::kBusiness252;
    }
    ThrowParseError(
            "day_count", value,
            "Actual360, Actual365Fixed, Thirty360, ActualActualIsda, ActualActualBond, Business252");
}

}  // namespace

ScheduleConfig ScheduleConfigResolver::Resolve(const TradeScheduleOverrides& overrides,
                                               const utils::Config& config) {
    ScheduleConfig out{};
    out.calendar_type =
            ParseCalendarType(EffectiveCalendarString(overrides, config));
    out.frequency = ParseFrequency(EffectiveFrequencyString(overrides, config));
    out.convention = ParseDateConvention(EffectiveConventionString(overrides, config));
    out.rule = ParseDateGenerationRule(EffectiveRuleString(overrides, config));
    out.day_count = ParseDayCount(EffectiveDayCountString(overrides, config));
    return out;
}

}  // namespace numeraire::schedule
