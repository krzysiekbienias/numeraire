#pragma once

#include <numeraire/enums/calendar_type.hpp>
#include <numeraire/enums/date_convention.hpp>
#include <numeraire/enums/date_generation_rule.hpp>
#include <numeraire/enums/day_count.hpp>
#include <numeraire/enums/frequency.hpp>

namespace numeraire::schedule {

/// Parameters for `ScheduleGenerator` (defaults match `configs/default.json` schedule block).
struct ScheduleConfig {
    CalendarType calendar_type{CalendarType::kUnitedKingdom};
    Frequency frequency{Frequency::kMonthly};
    DateConvention convention{DateConvention::kFollowing};
    DateGenerationRule rule{DateGenerationRule::kForward};
    DayCount day_count{DayCount::kActual365Fixed};
};

}  // namespace numeraire::schedule
