#include <numeraire/schedule/format_iso_date.hpp>

#include <cstdio>

namespace numeraire::schedule {

std::string FormatIsoDate(const Date& date) {
    char buf[11];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", date.year, date.month, date.day);
    return std::string(buf);
}

}  // namespace numeraire::schedule
