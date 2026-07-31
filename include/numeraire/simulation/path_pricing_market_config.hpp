#pragma once

#include <numeraire/database/discount_curve_eod_read.hpp>
#include <numeraire/database/vol_surface_eod_read.hpp>
#include <numeraire/simulation/path_pricing_quotes.hpp>

#include <optional>
#include <span>
#include <string>
#include <unordered_map>

namespace numeraire::simulation {

/// Sticky @ valuation `as_of` market inputs for path-wise repricing (FO-consistent).
struct PathPricingMarketConfig {
    /// Env flat fallbacks when DB rows are missing.
    PathPricingQuotes flat_fallbacks{};
    std::unordered_map<std::string, double> dividend_yields;
    std::unordered_map<std::string, database::VolSurfaceEodRead> vol_surfaces;
    std::optional<database::DiscountCurveEodRead> discount_curve;
    /// Audit tags, e.g. `IV_DB;R_DB;Q_ENV`.
    std::string quote_remarks;
};

/// Load vol surfaces and discount curve from SQLite (sticky at `as_of_iso`).
[[nodiscard]] PathPricingMarketConfig LoadPathPricingMarketConfig(
        const std::string& database_file_path,
        std::span<const std::string> underlying_ids,
        std::string_view as_of_iso,
        std::string_view discount_curve_id,
        const PathPricingQuotes& flat_fallbacks);

}  // namespace numeraire::simulation
