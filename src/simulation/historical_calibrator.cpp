#include <numeraire/simulation/historical_calibrator.hpp>

#include <numeraire/database/futures_pillar_returns.hpp>
#include <numeraire/database/underlying_daily_closes.hpp>
#include <numeraire/quant/cholesky.hpp>
#include <numeraire/quant/nearest_correlation.hpp>
#include <numeraire/schedule/format_iso_date.hpp>
#include <numeraire/utils/exception.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace numeraire::simulation {
namespace {

constexpr double kTol = 1.0e-12;

[[nodiscard]] std::optional<double> CloseOnDate(const std::vector<database::DailyCloseObservation>& series,
                                                const std::string& as_of_iso) {
    for (const database::DailyCloseObservation& bar : series) {
        if (bar.as_of == as_of_iso) {
            return bar.close;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::set<std::string> CommonDates(
        const std::vector<std::string>& factor_ids,
        const std::unordered_map<std::string, std::vector<database::DailyCloseObservation>>& closes_by_factor) {
    std::set<std::string> dates;
    bool first = true;
    for (const std::string& factor : factor_ids) {
        const auto it = closes_by_factor.find(factor);
        if (it == closes_by_factor.end()) {
            throw ValidationError("CalibrateFromPriceHistory: missing price history for factor=" + factor);
        }
        std::set<std::string> factor_dates;
        for (const database::DailyCloseObservation& bar : it->second) {
            factor_dates.insert(bar.as_of);
        }
        if (first) {
            dates = std::move(factor_dates);
            first = false;
        } else {
            std::set<std::string> intersection;
            std::set_intersection(dates.begin(), dates.end(), factor_dates.begin(), factor_dates.end(),
                                  std::inserter(intersection, intersection.begin()));
            dates = std::move(intersection);
        }
    }
    return dates;
}

[[nodiscard]] double Mean(const std::vector<double>& values) {
    if (values.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (const double v : values) {
        sum += v;
    }
    return sum / static_cast<double>(values.size());
}

[[nodiscard]] double SampleStdev(const std::vector<double>& values, const double mean) {
    if (values.size() < 2U) {
        return 0.0;
    }
    double acc = 0.0;
    for (const double v : values) {
        const double d = v - mean;
        acc += d * d;
    }
    return std::sqrt(acc / static_cast<double>(values.size() - 1U));
}

[[nodiscard]] double PearsonCorrelation(const std::vector<double>& x, const std::vector<double>& y) {
    if (x.size() != y.size() || x.size() < 2U) {
        return 0.0;
    }
    const double mean_x = Mean(x);
    const double mean_y = Mean(y);
    double cov = 0.0;
    double var_x = 0.0;
    double var_y = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        const double dx = x[i] - mean_x;
        const double dy = y[i] - mean_y;
        cov += dx * dy;
        var_x += dx * dx;
        var_y += dy * dy;
    }
    if (var_x <= kTol || var_y <= kTol) {
        return 0.0;
    }
    return cov / std::sqrt(var_x * var_y);
}

void ValidateConfig(const HistoricalCalibratorConfig& config) {
    if (config.lookback_calendar_days <= 0) {
        throw ValidationError("HistoricalCalibratorConfig: lookback_calendar_days must be > 0.");
    }
    if (config.min_return_observations < 2U) {
        throw ValidationError("HistoricalCalibratorConfig: min_return_observations must be >= 2.");
    }
    if (config.vol_annualization_days <= 0) {
        throw ValidationError("HistoricalCalibratorConfig: vol_annualization_days must be > 0.");
    }
    if (config.adjusted != 0 && config.adjusted != 1) {
        throw ValidationError("HistoricalCalibratorConfig: adjusted must be 0 or 1.");
    }
}

[[nodiscard]] HistoricalCalibratorConfig WithValidatedAsOf(const HistoricalCalibratorConfig& config) {
    ValidateConfig(config);
    if (config.as_of.year <= 0) {
        throw ValidationError("HistoricalCalibratorConfig: as_of must be set.");
    }
    return config;
}

/// Annualized vols, Pearson correlation, PSD repair and Cholesky, shared by the
/// close-history and return-history entry points.
void FillVolCorrelationAndCholesky(const std::vector<std::vector<double>>& returns_by_factor,
                                   const HistoricalCalibratorConfig& cfg,
                                   const std::string& context,
                                   HistoricalCalibrationResult& result) {
    const std::size_t n = returns_by_factor.size();
    const double annualization_scale = std::sqrt(static_cast<double>(cfg.vol_annualization_days));
    result.volatilities.resize(n);
    for (std::size_t f = 0; f < n; ++f) {
        const double daily_vol = SampleStdev(returns_by_factor[f], Mean(returns_by_factor[f]));
        result.volatilities[f] = daily_vol * annualization_scale;
    }

    result.correlation.assign(n * n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        result.correlation[(i * n) + i] = 1.0;
        for (std::size_t j = 0; j < i; ++j) {
            const double rho = PearsonCorrelation(returns_by_factor[i], returns_by_factor[j]);
            result.correlation[(i * n) + j] = rho;
            result.correlation[(j * n) + i] = rho;
        }
    }

    auto nearest = quant::NearestCorrelationHigham(result.correlation, n);
    if (nearest.status != quant::NearestCorrelationStatus::kOk) {
        throw ValidationError(context + ": nearest correlation repair failed.");
    }
    result.correlation = nearest.matrix;

    constexpr int kMaxCholeskyAttempts = 8;
    constexpr double kOffDiagShrink = 1.0 - 1.0e-8;
    quant::CholeskyResult chol;
    for (int attempt = 0; attempt < kMaxCholeskyAttempts; ++attempt) {
        chol = quant::CholeskyDecompose(result.correlation, n);
        if (chol.status == quant::CholeskyStatus::kOk) {
            break;
        }
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t j = 0; j < n; ++j) {
                if (i != j) {
                    result.correlation[(i * n) + j] *= kOffDiagShrink;
                }
            }
        }
        nearest = quant::NearestCorrelationHigham(result.correlation, n);
        if (nearest.status != quant::NearestCorrelationStatus::kOk) {
            throw ValidationError(context + ": nearest correlation repair failed.");
        }
        result.correlation = nearest.matrix;
    }
    if (chol.status != quant::CholeskyStatus::kOk) {
        throw ValidationError(context + ": Cholesky decomposition failed.");
    }
    result.cholesky = chol.factor;
}

}  // namespace

HistoricalCalibrationResult CalibrateFromPriceHistory(
        const std::vector<std::string>& factor_ids,
        const std::unordered_map<std::string, std::vector<database::DailyCloseObservation>>& closes_by_factor,
        const HistoricalCalibratorConfig& config) {
    const HistoricalCalibratorConfig cfg = WithValidatedAsOf(config);
    if (factor_ids.empty()) {
        throw ValidationError("CalibrateFromPriceHistory: factor_ids must not be empty.");
    }

    const std::string as_of_iso = schedule::FormatIsoDate(cfg.as_of);
    HistoricalCalibrationResult result;
    result.factor_ids = factor_ids;
    result.spots_as_of.reserve(factor_ids.size());
    for (const std::string& factor : factor_ids) {
        const auto it = closes_by_factor.find(factor);
        if (it == closes_by_factor.end()) {
            throw ValidationError("CalibrateFromPriceHistory: missing price history for factor=" + factor);
        }
        const std::optional<double> spot = CloseOnDate(it->second, as_of_iso);
        if (!spot.has_value() || *spot <= 0.0) {
            throw ValidationError("CalibrateFromPriceHistory: missing positive spot on as_of for factor=" + factor);
        }
        result.spots_as_of.push_back(*spot);
    }

    const std::set<std::string> common_dates = CommonDates(factor_ids, closes_by_factor);
    if (common_dates.size() < cfg.min_return_observations + 1U) {
        throw ValidationError("CalibrateFromPriceHistory: insufficient aligned price observations.");
    }

    std::map<std::string, double> close_by_date;
    const std::size_t n = factor_ids.size();
    std::vector<std::vector<double>> returns_by_factor(n);
    std::string previous_date;
    for (const std::string& date : common_dates) {
        close_by_date.clear();
        for (const std::string& factor : factor_ids) {
            const auto& series = closes_by_factor.at(factor);
            const auto bar_it = std::find_if(series.begin(), series.end(),
                                             [&](const database::DailyCloseObservation& bar) {
                                                 return bar.as_of == date;
                                             });
            if (bar_it == series.end() || bar_it->close <= 0.0) {
                throw ValidationError("CalibrateFromPriceHistory: missing close on common date=" + date);
            }
            close_by_date[factor] = bar_it->close;
        }
        if (!previous_date.empty()) {
            for (std::size_t f = 0; f < n; ++f) {
                const std::string& factor = factor_ids[f];
                const auto& series = closes_by_factor.at(factor);
                const auto prev_bar_it = std::find_if(
                        series.begin(), series.end(), [&](const database::DailyCloseObservation& bar) {
                            return bar.as_of == previous_date;
                        });
                if (prev_bar_it == series.end() || prev_bar_it->close <= 0.0) {
                    throw ValidationError("CalibrateFromPriceHistory: missing previous close on date=" +
                                          previous_date);
                }
                const double log_return = std::log(close_by_date.at(factor) / prev_bar_it->close);
                returns_by_factor[f].push_back(log_return);
            }
        }
        previous_date = date;
    }

    result.num_return_observations = returns_by_factor.front().size();
    if (result.num_return_observations < cfg.min_return_observations) {
        throw ValidationError("CalibrateFromPriceHistory: insufficient aligned log-return observations.");
    }

    result.history_start = schedule::ParseIsoDate(*common_dates.begin());
    result.history_end = schedule::ParseIsoDate(*common_dates.rbegin());

    FillVolCorrelationAndCholesky(returns_by_factor, cfg, "CalibrateFromPriceHistory", result);
    return result;
}

HistoricalCalibrationResult CalibrateFromReturnHistory(const std::vector<FactorReturnHistory>& factors,
                                                       const HistoricalCalibratorConfig& config) {
    const HistoricalCalibratorConfig cfg = WithValidatedAsOf(config);
    if (factors.empty()) {
        throw ValidationError("CalibrateFromReturnHistory: factors must not be empty.");
    }

    std::set<std::string> common_dates;
    bool first = true;
    for (const FactorReturnHistory& factor : factors) {
        std::set<std::string> factor_dates;
        for (const FactorReturnObservation& observation : factor.returns) {
            factor_dates.insert(observation.as_of);
        }
        if (first) {
            common_dates = std::move(factor_dates);
            first = false;
        } else {
            std::set<std::string> intersection;
            std::set_intersection(common_dates.begin(), common_dates.end(), factor_dates.begin(),
                                  factor_dates.end(), std::inserter(intersection, intersection.begin()));
            common_dates = std::move(intersection);
        }
    }
    if (common_dates.size() < cfg.min_return_observations) {
        throw ValidationError("CalibrateFromReturnHistory: insufficient aligned log-return observations (" +
                              std::to_string(common_dates.size()) + " < " +
                              std::to_string(cfg.min_return_observations) + ").");
    }

    const std::size_t n = factors.size();
    HistoricalCalibrationResult result;
    result.factor_ids.reserve(n);
    result.spots_as_of.reserve(n);
    std::vector<std::vector<double>> returns_by_factor(n);

    for (std::size_t f = 0; f < n; ++f) {
        const FactorReturnHistory& factor = factors[f];
        if (factor.level_as_of <= 0.0) {
            throw ValidationError("CalibrateFromReturnHistory: missing positive level for factor=" +
                                  factor.factor_id);
        }
        result.factor_ids.push_back(factor.factor_id);
        result.spots_as_of.push_back(factor.level_as_of);

        std::map<std::string, double> by_date;
        for (const FactorReturnObservation& observation : factor.returns) {
            by_date[observation.as_of] = observation.log_return;
        }
        returns_by_factor[f].reserve(common_dates.size());
        for (const std::string& date : common_dates) {
            returns_by_factor[f].push_back(by_date.at(date));
        }
    }

    result.num_return_observations = common_dates.size();
    result.history_start = schedule::ParseIsoDate(*common_dates.begin());
    result.history_end = schedule::ParseIsoDate(*common_dates.rbegin());

    FillVolCorrelationAndCholesky(returns_by_factor, cfg, "CalibrateFromReturnHistory", result);
    return result;
}

HistoricalCalibrationResult CalibrateFromDatabase(const std::string& database_file_path,
                                                  const std::vector<std::string>& factor_ids,
                                                  const HistoricalCalibratorConfig& config) {
    const HistoricalCalibratorConfig cfg = WithValidatedAsOf(config);
    if (factor_ids.empty()) {
        throw ValidationError("CalibrateFromDatabase: factor_ids must not be empty.");
    }

    const schedule::Date history_start_date = schedule::AddCalendarDays(cfg.as_of, -cfg.lookback_calendar_days);
    const std::string from_iso = schedule::FormatIsoDate(history_start_date);
    const std::string to_iso = schedule::FormatIsoDate(cfg.as_of);

    std::unordered_map<std::string, std::vector<database::DailyCloseObservation>> closes_by_factor;
    closes_by_factor.reserve(factor_ids.size());
    for (const std::string& factor : factor_ids) {
        std::vector<database::DailyCloseObservation> series = database::LoadUnderlyingDailyClosesRange(
                database_file_path, factor, from_iso, to_iso, cfg.adjusted);
        if (series.empty()) {
            throw ValidationError("CalibrateFromDatabase: no EOD history for factor=" + factor + " in [" + from_iso +
                                  ", " + to_iso + "].");
        }
        closes_by_factor.emplace(factor, std::move(series));
    }
    return CalibrateFromPriceHistory(factor_ids, closes_by_factor, cfg);
}

namespace {

/// One equity / index underlying becomes one factor: consecutive closes differenced
/// into returns, anchored on its close on `as_of`.
[[nodiscard]] FactorReturnHistory LoadEquityFactor(const std::string& database_file_path,
                                                   const std::string& underlying_id,
                                                   const std::string& from_iso,
                                                   const std::string& to_iso,
                                                   const HistoricalCalibratorConfig& cfg) {
    const std::vector<database::DailyCloseObservation> series = database::LoadUnderlyingDailyClosesRange(
            database_file_path, underlying_id, from_iso, to_iso, cfg.adjusted);
    if (series.empty()) {
        throw ValidationError("CalibrateBookFromDatabase: no EOD history for equity underlying=" + underlying_id +
                              " in [" + from_iso + ", " + to_iso + "].");
    }
    const std::optional<double> level = CloseOnDate(series, to_iso);
    if (!level.has_value() || *level <= 0.0) {
        throw ValidationError("CalibrateBookFromDatabase: no positive close on as_of=" + to_iso +
                              " for equity underlying=" + underlying_id + ".");
    }

    FactorReturnHistory factor;
    factor.factor_id = underlying_id;
    factor.level_as_of = *level;
    factor.returns.reserve(series.size());
    for (std::size_t i = 1; i < series.size(); ++i) {
        if (series[i - 1].close <= 0.0 || series[i].close <= 0.0) {
            continue;
        }
        factor.returns.push_back(FactorReturnObservation{
                .as_of = series[i].as_of,
                .log_return = std::log(series[i].close / series[i - 1].close),
        });
    }
    return factor;
}

/// One commodity curve becomes a strip of constant-maturity pillar factors, so the
/// legs of a calendar spread are driven by different (highly but not perfectly
/// correlated) factors instead of collapsing onto one.
void AppendCommodityPillarFactors(const std::string& database_file_path,
                                  const std::string& product_code,
                                  const std::string& from_iso,
                                  const std::string& to_iso,
                                  const HistoricalCalibratorConfig& cfg,
                                  std::vector<FactorReturnHistory>& out) {
    const std::vector<database::FuturesPillarSeries> pillars = database::LoadFuturesPillarReturns(
            database_file_path, product_code, from_iso, to_iso, cfg.commodity_pillars);
    if (pillars.empty()) {
        throw ValidationError("CalibrateBookFromDatabase: no futures pillar history for commodity underlying=" +
                              product_code + " in [" + from_iso + ", " + to_iso + "].");
    }

    for (const database::FuturesPillarSeries& pillar : pillars) {
        if (pillar.last_date != to_iso) {
            throw ValidationError("CalibrateBookFromDatabase: " + pillar.factor_id + " has no session on as_of=" +
                                  to_iso + " (latest is " + pillar.last_date + ").");
        }
        FactorReturnHistory factor;
        factor.factor_id = pillar.factor_id;
        factor.level_as_of = pillar.level_on_last_date;
        factor.returns.reserve(pillar.returns.size());
        for (const database::PillarReturn& observation : pillar.returns) {
            factor.returns.push_back(FactorReturnObservation{
                    .as_of = observation.as_of,
                    .log_return = observation.log_return,
            });
        }
        out.push_back(std::move(factor));
    }
}

}  // namespace

HistoricalCalibrationResult CalibrateBookFromDatabase(const std::string& database_file_path,
                                                      const HistoricalCalibratorConfig& config,
                                                      const std::optional<std::string_view> portfolio_id) {
    const HistoricalCalibratorConfig cfg = WithValidatedAsOf(config);
    const std::vector<database::BookUnderlying> underlyings =
            database::ListBookUnderlyings(database_file_path, std::string_view{"LIVE"}, portfolio_id);
    if (underlyings.empty()) {
        if (portfolio_id.has_value()) {
            throw ValidationError("CalibrateBookFromDatabase: portfolio has no underlyings: portfolio_id=" +
                                  std::string(*portfolio_id));
        }
        throw ValidationError("CalibrateBookFromDatabase: book has no underlyings.");
    }

    const std::string from_iso =
            schedule::FormatIsoDate(schedule::AddCalendarDays(cfg.as_of, -cfg.lookback_calendar_days));
    const std::string to_iso = schedule::FormatIsoDate(cfg.as_of);

    std::vector<FactorReturnHistory> factors;
    factors.reserve(underlyings.size());
    for (const database::BookUnderlying& underlying : underlyings) {
        if (underlying.asset_kind == "COMMODITY") {
            AppendCommodityPillarFactors(database_file_path, underlying.underlying_id, from_iso, to_iso, cfg,
                                         factors);
        } else {
            factors.push_back(LoadEquityFactor(database_file_path, underlying.underlying_id, from_iso, to_iso, cfg));
        }
    }
    return CalibrateFromReturnHistory(factors, cfg);
}

}  // namespace numeraire::simulation
