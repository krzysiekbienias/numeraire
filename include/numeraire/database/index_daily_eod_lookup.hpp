#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace numeraire::database {

[[nodiscard]] std::optional<double> LookupIndexDailyClose(const std::string& database_file_path,
                                                          std::string_view ticker,
                                                          std::string_view as_of_iso_yyyy_mm_dd,
                                                          int adjusted = 1);

}  // namespace numeraire::database
