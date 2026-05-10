#pragma once

#include <numeraire/schedule/schedule.hpp>
#include <numeraire/schedule/schedule_config.hpp>

#include <ql/time/schedule.hpp>

namespace numeraire::schedule {

/// Wraps stored dates using metadata from `config` (calendar, convention, …).
[[nodiscard]] QuantLib::Schedule ScheduleToQuantLib(const Schedule& schedule, const ScheduleConfig& config);

/// Copies QuantLib dates into a `Schedule` tagged with `day_count`.
[[nodiscard]] Schedule ScheduleFromQuantLib(const QuantLib::Schedule& ql_schedule, DayCount day_count);

}  // namespace numeraire::schedule
