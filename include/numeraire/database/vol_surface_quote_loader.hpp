#pragma once

#include <numeraire/database/vol_surface_types.hpp>

#include <string>
#include <vector>

namespace numeraire::database {

/// Loads option EOD closes joined to catalog metadata for one underlying and session date.
[[nodiscard]] std::vector<VolSurfaceOptionQuoteInput> LoadVolSurfaceOptionQuotes(
        const std::string& database_file_path,
        std::string_view underlying_ticker,
        std::string_view as_of_iso_yyyy_mm_dd,
        int adjusted = 1);

}  // namespace numeraire::database
