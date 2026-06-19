#include <numeraire/simulation/historical_calibrator.hpp>

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
        throw ValidationError("CalibrateFromPriceHistory: nearest correlation repair failed.");
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
            throw ValidationError("CalibrateFromPriceHistory: nearest correlation repair failed.");
        }
        result.correlation = nearest.matrix;
    }
    if (chol.status != quant::CholeskyStatus::kOk) {
        throw ValidationError("CalibrateFromPriceHistory: Cholesky decomposition failed.");
    }
    result.cholesky = chol.factor;
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

HistoricalCalibrationResult CalibrateBookFromDatabase(const std::string& database_file_path,
                                                      const HistoricalCalibratorConfig& config,
                                                      const std::optional<std::string_view> portfolio_id) {
    const std::vector<std::string> factor_ids =
            database::ListDistinctBookUnderlyingIds(database_file_path, std::string_view{"LIVE"}, portfolio_id);
    if (factor_ids.empty()) {
        if (portfolio_id.has_value()) {
            throw ValidationError("CalibrateBookFromDatabase: portfolio has no underlyings: portfolio_id=" +
                                  std::string(*portfolio_id));
        }
        throw ValidationError("CalibrateBookFromDatabase: book has no underlyings.");
    }
    return CalibrateFromDatabase(database_file_path, factor_ids, config);
}

}  // namespace numeraire::simulation
