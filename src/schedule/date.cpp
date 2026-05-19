#include <numeraire/schedule/date.hpp>
#include <numeraire/utils/exception.hpp>
#include <numeraire/utils/string.hpp>

#include <cstdio>
#include <ql/time/daycounters/actual365fixed.hpp>

namespace numeraire::schedule {

QuantLib::Date ToQuantLibDate(const Date& date) {
    return QuantLib::Date(static_cast<QuantLib::Day>(date.day), static_cast<QuantLib::Month>(date.month),
            static_cast<QuantLib::Year>(date.year));
}

Date FromQuantLibDate(const QuantLib::Date& date) {
    return Date{static_cast<int>(date.year()), static_cast<int>(date.month()), static_cast<int>(date.dayOfMonth())};
}

double Act365FixedYearFraction(const Date& start, const Date& end) {
    return QuantLib::Actual365Fixed().yearFraction(ToQuantLibDate(start), ToQuantLibDate(end));
}

[[nodiscard]] numeraire::schedule::Date ParseIsoDate(const std::string& raw) {
    const std::string s = utils::TrimCopy(raw);
    if (s.size() != 10U || s[4] != '-' || s[7] != '-') {
        throw numeraire::ValidationError("expected ISO date YYYY-MM-DD, got: " + raw);
    }
    int year{};
    int month{};
    int day{};
    const char* const p = s.c_str();
    if (std::sscanf(p, "%d-%d-%d", &year, &month, &day) != 3) {
        throw numeraire::ValidationError("invalid ISO date: " + raw);
    }
    if (month < 1 || month > 12 || day < 1 || day > 31) {
        throw numeraire::ValidationError("date out of range: " + raw);
    }
    return numeraire::schedule::Date{.year = year, .month = month, .day = day};
}

}  // namespace numeraire::schedule
