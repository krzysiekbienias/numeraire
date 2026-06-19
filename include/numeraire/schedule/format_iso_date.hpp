#pragma once

#include <numeraire/schedule/date.hpp>

#include <string>

namespace numeraire::schedule {

[[nodiscard]] std::string FormatIsoDate(const Date& date);

}  // namespace numeraire::schedule
