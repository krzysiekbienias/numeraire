#include <numeraire/simulation/path_pricing_market_config.hpp>

#include <numeraire/database/discount_curve_eod_read.hpp>
#include <numeraire/database/vol_surface_eod_read.hpp>
#include <numeraire/utils/exception.hpp>
#include <numeraire/utils/logger.hpp>

#include <sstream>
#include <string>

namespace numeraire::simulation {

namespace {

using numeraire::utils::Logger;

[[nodiscard]] std::string JoinCsv(const std::vector<std::string>& ids) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (i > 0U) {
            oss << ',';
        }
        oss << ids[i];
    }
    return oss.str();
}

}  // namespace

PathPricingMarketConfig LoadPathPricingMarketConfig(
        const std::string& database_file_path,
        const std::span<const std::string> underlying_ids,
        const std::string_view as_of_iso,
        const std::string_view discount_curve_id,
        const PathPricingQuotes& flat_fallbacks) {
    if (as_of_iso.empty()) {
        throw ValidationError("LoadPathPricingMarketConfig: as_of_iso must be non-empty.");
    }

    PathPricingMarketConfig config{};
    config.flat_fallbacks = flat_fallbacks;

    std::vector<std::string> iv_db_ids;
    std::vector<std::string> iv_env_ids;
    iv_db_ids.reserve(underlying_ids.size());
    iv_env_ids.reserve(underlying_ids.size());

    for (const std::string& underlying_id : underlying_ids) {
        std::optional<database::VolSurfaceEodRead> surface =
                database::TryLoadVolSurfaceEod(database_file_path, underlying_id, as_of_iso);
        if (!surface.has_value()) {
            iv_env_ids.push_back(underlying_id);
            config.dividend_yields[underlying_id] = flat_fallbacks.dividend_yield;
            continue;
        }
        iv_db_ids.push_back(underlying_id);
        config.dividend_yields[underlying_id] = surface->dividend_yield;
        config.vol_surfaces.emplace(underlying_id, std::move(*surface));
    }

    config.discount_curve =
            database::TryLoadLatestDiscountCurveEod(database_file_path, std::string(discount_curve_id), as_of_iso);

    std::ostringstream remarks;
    if (!iv_db_ids.empty()) {
        remarks << "IV_DB;";
    }
    if (!iv_env_ids.empty()) {
        remarks << "IV_ENV;";
    }
    if (config.discount_curve.has_value()) {
        remarks << "R_DB;";
    } else {
        remarks << "R_ENV;";
    }
    remarks << "Q_ENV";
    config.quote_remarks = remarks.str();

    Logger::NumInfo(
            "Path pricing quotes as_of={}: IV {} from vol_surface_eod; IV {} env flat={}; rate {} (curve_id={}).",
            as_of_iso,
            JoinCsv(iv_db_ids),
            JoinCsv(iv_env_ids),
            flat_fallbacks.flat_implied_volatility,
            config.discount_curve.has_value() ? "db" : "env",
            discount_curve_id);

    if (config.discount_curve.has_value()) {
        Logger::NumInfo("Path pricing discount curve loaded @ {} (requested as_of={}).",
                        config.discount_curve->as_of,
                        as_of_iso);
    }

    return config;
}

}  // namespace numeraire::simulation
