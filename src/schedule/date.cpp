#include <numeraire/schedule/date.hpp>

namespace numeraire::schedule {

QuantLib::Date ToQuantLibDate(const Date& date) {
    return QuantLib::Date(static_cast<QuantLib::Day>(date.day), static_cast<QuantLib::Month>(date.month),
            static_cast<QuantLib::Year>(date.year));
}

Date FromQuantLibDate(const QuantLib::Date& date) {
    return Date{static_cast<int>(date.year()), static_cast<int>(date.month()), static_cast<int>(date.dayOfMonth())};
}

}  // namespace numeraire::schedule
