#include <numeraire/market_data/sqlite_vol_surface_market_data.hpp>

#include <numeraire/database/vol_surface_eod_read.hpp>
#include <numeraire/market_data/vol_surface_interpolation.hpp>
#include <numeraire/utils/exception.hpp>

#include <cmath>
#include <string>
#include <utility>

namespace numeraire::market_data {

SqliteVolSurfaceMarketData::SqliteVolSurfaceMarketData(
        schedule::Date valuation_date,
        std::unordered_map<std::string, double> spots,
        const double risk_free_rate,
        std::unordered_map<std::string, double> dividend_yields,
        std::unordered_map<std::string, database::VolSurfaceEodRead> surfaces)
        : valuation_date_(valuation_date),
          spots_(std::move(spots)),
          risk_free_rate_(risk_free_rate),
          dividend_yields_(std::move(dividend_yields)),
          surfaces_(std::move(surfaces)) {}

std::unique_ptr<SqliteVolSurfaceMarketData> SqliteVolSurfaceMarketData::Load(
        const std::string& database_file_path,
        schedule::Date valuation_date,
        std::unordered_map<std::string, double> spots,
        const double risk_free_rate,
        std::unordered_map<std::string, double> dividend_yields,
        const std::vector<std::string>& underlying_ids,
        const std::string_view as_of_iso_yyyy_mm_dd,
        const std::string_view surface_kind) {
    std::unordered_map<std::string, database::VolSurfaceEodRead> surfaces;
    for (const std::string& underlying_id : underlying_ids) {
        database::VolSurfaceEodRead leg =
                database::LoadVolSurfaceEod(database_file_path, underlying_id, as_of_iso_yyyy_mm_dd, surface_kind);
        if (dividend_yields.find(underlying_id) == dividend_yields.end()) {
            dividend_yields[underlying_id] = leg.dividend_yield;
        }
        surfaces.emplace(underlying_id, std::move(leg));
    }

    return std::make_unique<SqliteVolSurfaceMarketData>(valuation_date, std::move(spots), risk_free_rate,
                                                        std::move(dividend_yields), std::move(surfaces));
}

const schedule::Date& SqliteVolSurfaceMarketData::ValuationDate() const { return valuation_date_; }

double SqliteVolSurfaceMarketData::Spot(const std::string_view underlying_id) const {
    const std::string key(underlying_id);
    const auto it = spots_.find(key);
    if (it == spots_.end()) {
        throw MarketDataError("Spot: unknown underlying \"" + key + "\"");
    }
    return it->second;
}

double SqliteVolSurfaceMarketData::RiskFreeRate() const { return risk_free_rate_; }

double SqliteVolSurfaceMarketData::DividendYield(const std::string_view underlying_id) const {
    const std::string key(underlying_id);
    const auto it = dividend_yields_.find(key);
    if (it == dividend_yields_.end()) {
        return 0.0;
    }
    return it->second;
}

const database::VolSurfaceEodRead* SqliteVolSurfaceMarketData::FindSurface(
        const std::string_view underlying_id) const noexcept {
    const auto it = surfaces_.find(std::string(underlying_id));
    if (it == surfaces_.end()) {
        return nullptr;
    }
    return &it->second;
}

double SqliteVolSurfaceMarketData::ImpliedVolatility(const std::string_view underlying_id,
                                                     const double strike,
                                                     const double time_to_expiry_years,
                                                     const OptionType option_kind) const {
    const database::VolSurfaceEodRead* leg = FindSurface(underlying_id);
    if (leg == nullptr) {
        throw MarketDataError("ImpliedVolatility: no vol surface for underlying \"" + std::string(underlying_id) +
                              "\"");
    }
    if (strike <= 0.0) {
        throw MarketDataError("ImpliedVolatility: strike must be positive");
    }
    if (time_to_expiry_years <= 0.0) {
        return 0.0;
    }

    const double ln_m = std::log(strike / leg->spot_used);
    const auto& points = option_kind == OptionType::kCall ? leg->call_points : leg->put_points;
    if (points.empty()) {
        throw MarketDataError("ImpliedVolatility: empty " + std::string(option_kind == OptionType::kCall ? "call" : "put") +
                              " surface for \"" + std::string(underlying_id) + "\"");
    }

    return InterpolateImpliedVol(points, ln_m, time_to_expiry_years);
}

}  // namespace numeraire::market_data
