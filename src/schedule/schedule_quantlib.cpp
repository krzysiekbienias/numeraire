#include <numeraire/schedule/schedule_quantlib.hpp>

#include <numeraire/schedule/date.hpp>
#include <numeraire/utils/quantlib_bridge.hpp>

#include <utility>
#include <vector>

namespace numeraire::schedule {

QuantLib::Schedule ScheduleToQuantLib(const Schedule& schedule, const ScheduleConfig& config) {
    std::vector<QuantLib::Date> ql_dates;
    ql_dates.reserve(schedule.dates.size());
    for (const Date& d : schedule.dates) {
        ql_dates.push_back(ToQuantLibDate(d));
    }
    return QuantLib::Schedule(std::move(ql_dates), numeraire::utils::quantlib_bridge::ToQuantLib(config.calendar_type),
            numeraire::utils::quantlib_bridge::ToQuantLib(config.convention), QuantLib::ext::nullopt,
            QuantLib::ext::nullopt, QuantLib::ext::nullopt, QuantLib::ext::nullopt, std::vector<bool>{});
}

Schedule ScheduleFromQuantLib(const QuantLib::Schedule& ql_schedule, const DayCount day_count) {
    Schedule out;
    out.day_count = day_count;
    out.dates.reserve(ql_schedule.size());
    for (const QuantLib::Date& d : ql_schedule.dates()) {
        out.dates.push_back(FromQuantLibDate(d));
    }
    return out;
}

}  // namespace numeraire::schedule
