#pragma once

#include <ql/time/date.hpp>

#include <string>

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

[[nodiscard]] Date ParseIsoDate(const std::string& raw);

/// Year fraction between `start` and `end` on **Actual / 365 Fixed**, matching
/// `QuantLib::Actual365Fixed`. Used for option horizons (pricing) and schedules;
/// negative when `end` precedes `start`.
[[nodiscard]] double Act365FixedYearFraction(const Date& start, const Date& end);

/// Calendar advance by whole days (QuantLib serial-date arithmetic; no holiday adjust).
[[nodiscard]] Date AddCalendarDays(const Date& start, int days);

}  // namespace numeraire::schedule
