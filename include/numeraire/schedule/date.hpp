#pragma once

#include <ql/time/date.hpp>

namespace numeraire::schedule {

/// Calendar date in the public API (Gregorian). Maps to `QuantLib::Date` for
/// schedule generation; fields use 1-based month and day like QuantLib.
struct Date {
    int year{};
    int month{};
    int day{};
};

[[nodiscard]] QuantLib::Date ToQuantLibDate(const Date& date);
[[nodiscard]] Date FromQuantLibDate(const QuantLib::Date& date);

}  // namespace numeraire::schedule
