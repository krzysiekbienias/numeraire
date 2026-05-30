#pragma once

#include <numeraire/database/discount_curve_types.hpp>

#include <optional>
#include <string>
#include <vector>

namespace numeraire::database {

[[nodiscard]] std::optional<ParCurveEodHeader> LoadParCurveEodHeader(const std::string& database_file_path,
                                                                     const std::string_view curve_id,
                                                                     const std::string_view as_of_iso_yyyy_mm_dd);

[[nodiscard]] std::vector<ParCurvePillarInput> LoadParCurvePillarInputs(const std::string& database_file_path,
                                                                          const std::string_view curve_id,
                                                                          const std::string_view as_of_iso_yyyy_mm_dd);

}  // namespace numeraire::database
