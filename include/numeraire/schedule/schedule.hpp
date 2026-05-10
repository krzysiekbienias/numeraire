#pragma once

#include <numeraire/enums/day_count.hpp>
#include <numeraire/schedule/date.hpp>

#include <vector>

namespace numeraire::schedule {

/// Lightweight payment / fixing schedule as a list of dates plus day-count tag.
struct Schedule {
    std::vector<Date> dates;
    DayCount day_count{DayCount::kActual365Fixed};

    [[nodiscard]] bool Empty() const noexcept { return dates.empty(); }
    [[nodiscard]] std::size_t Size() const noexcept { return dates.size(); }
};

}  // namespace numeraire::schedule
