#include <numeraire/simulation/gabillon_calibration_builder.hpp>

#include <numeraire/database/calibration_types.hpp>
#include <numeraire/database/futures_pillar_returns.hpp>
#include <numeraire/database/sqlite_calibration_repository.hpp>
#include <numeraire/database/sqlite_schema.hpp>
#include <numeraire/database/trade_lifecycle.hpp>
#include <numeraire/database/underlying_daily_closes.hpp>
#include <numeraire/quant/cholesky.hpp>
#include <numeraire/quant/gabillon_curve_fit.hpp>
#include <numeraire/quant/nearest_correlation.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/schedule/format_iso_date.hpp>
#include <numeraire/utils/config.hpp>
#include <numeraire/utils/database_path.hpp>
#include <numeraire/utils/exception.hpp>
#include <numeraire/utils/logger.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace numeraire::simulation {
namespace {

using numeraire::database::CalibrationCholeskyWrite;
using numeraire::database::CalibrationCorrelationWrite;
using numeraire::database::CalibrationFactorWrite;
using numeraire::database::CalibrationHeaderWrite;
using numeraire::database::CalibrationParamWrite;
using numeraire::database::SqliteCalibrationRepository;
using numeraire::utils::Logger;
using numeraire::utils::ResolveDatabasePath;

/// Daily moves of the two state variables, recovered from the pillar strip.
struct FactorReturnPair {
    double short_factor{0.0};
    double long_factor{0.0};
};

/// Everything one commodity curve contributes to the snapshot.
struct CurveCalibration {
    std::string product_code;
    quant::GabillonCurveFit curve_fit;
    quant::GabillonVolFit vol_fit;
    std::size_t num_vol_pillars{0};
    std::map<std::string, FactorReturnPair> factor_returns_by_date;
};

[[nodiscard]] bool LooksIsoDate(const std::string& s) {
    return s.size() == 10U && s[4] == '-' && s[7] == '-';
}

[[nodiscard]] int EnvInt(const char* key, const int default_value) {
    const char* raw = std::getenv(key);
    if (raw == nullptr || raw[0] == '\0') {
        return default_value;
    }
    char* end = nullptr;
    const std::int64_t v = std::strtoll(raw, &end, 10);
    return end == raw ? default_value : static_cast<int>(v);
}

[[nodiscard]] std::optional<std::string> EnvNonEmptyString(const char* key) {
    const char* raw = std::getenv(key);
    if (raw == nullptr || raw[0] == '\0') {
        return std::nullopt;
    }
    return std::string{raw};
}

/// Position of a date inside its own calendar year, which is the coordinate seasonality
/// repeats in. Time to settlement is not: a December contract is a December contract
/// whether it is three months or three years away.
[[nodiscard]] double YearPhase(const schedule::Date& date) {
    const schedule::Date jan_first{.year = date.year, .month = 1, .day = 1};
    return schedule::Act365FixedYearFraction(jan_first, date);
}

[[nodiscard]] double SampleStdDev(const std::vector<double>& values) {
    if (values.size() < 2U) {
        return 0.0;
    }
    double mean = 0.0;
    for (const double v : values) {
        mean += v;
    }
    mean /= static_cast<double>(values.size());
    double sum_sq = 0.0;
    for (const double v : values) {
        sum_sq += (v - mean) * (v - mean);
    }
    return std::sqrt(sum_sq / static_cast<double>(values.size() - 1U));
}

[[nodiscard]] double PearsonCorrelation(const std::vector<double>& a, const std::vector<double>& b) {
    const auto n = static_cast<double>(a.size());
    double mean_a = 0.0;
    double mean_b = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        mean_a += a[i];
        mean_b += b[i];
    }
    mean_a /= n;
    mean_b /= n;
    double cov = 0.0;
    double var_a = 0.0;
    double var_b = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const double da = a[i] - mean_a;
        const double db = b[i] - mean_b;
        cov += da * db;
        var_a += da * da;
        var_b += db * db;
    }
    if (!(var_a > 0.0) || !(var_b > 0.0)) {
        return 0.0;
    }
    return std::clamp(cov / std::sqrt(var_a * var_b), -1.0, 1.0);
}

/// Fit the seasonal shape and long-run structure to one session's forward curve.
[[nodiscard]] quant::GabillonCurveFit FitCurveShape(const std::string& db_path, const std::string& product_code,
                                                    const std::string& as_of, const int seasonal_harmonics) {
    const schedule::Date as_of_date = schedule::ParseIsoDate(as_of);
    const std::vector<database::FuturesCurveQuote> quotes =
            database::LoadFuturesCurveSnapshot(db_path, product_code, as_of);

    std::vector<quant::ForwardCurvePoint> curve;
    curve.reserve(quotes.size());
    for (const database::FuturesCurveQuote& quote : quotes) {
        const schedule::Date settle = schedule::ParseIsoDate(quote.settlement_date);
        curve.push_back(quant::ForwardCurvePoint{
                .time_to_settlement_years = schedule::Act365FixedYearFraction(as_of_date, settle),
                .year_phase = YearPhase(settle),
                .price = quote.price,
        });
    }

    const quant::GabillonCurveFit fit = quant::FitGabillonCurve(curve, seasonal_harmonics);
    if (fit.status != quant::GabillonFitStatus::kOk) {
        throw ValidationError("BuildGabillonCalibration: could not fit the " + product_code + " curve on " + as_of +
                              " (" + std::to_string(curve.size()) + " contracts quoted; a seasonal fit needs at " +
                              "least 2 + 2 x harmonics).");
    }
    return fit;
}

/// Annualized pillar volatilities and the maturities they sit at, plus the raw returns
/// keyed by session so factor moves can be extracted afterwards.
struct PillarStrip {
    std::vector<double> tenors;
    std::vector<double> volatilities;
    /// session -> (tenor, log return) for every pillar quoted that session.
    std::map<std::string, std::vector<std::pair<double, double>>> returns_by_date;
    std::string history_start;
    std::string history_end;
};

[[nodiscard]] PillarStrip LoadPillarStrip(const std::string& db_path, const std::string& product_code,
                                          const std::string& as_of, const std::string& from_date,
                                          const GabillonCalibrationBuildParams& params) {
    const schedule::Date as_of_date = schedule::ParseIsoDate(as_of);
    const std::vector<database::FuturesPillarSeries> series =
            database::LoadFuturesPillarReturns(db_path, product_code, from_date, as_of, params.commodity_pillars);
    const std::vector<database::FuturesPillarOccupant> occupants =
            database::LoadFuturesPillarCurve(db_path, product_code, as_of, params.commodity_pillars);

    std::map<int, double> tenor_by_pillar;
    for (const database::FuturesPillarOccupant& occupant : occupants) {
        tenor_by_pillar[occupant.pillar] =
                schedule::Act365FixedYearFraction(as_of_date, schedule::ParseIsoDate(occupant.settlement_date));
    }

    PillarStrip strip;
    for (const database::FuturesPillarSeries& pillar : series) {
        const auto tenor_it = tenor_by_pillar.find(pillar.pillar);
        if (tenor_it == tenor_by_pillar.end() || !(tenor_it->second > 0.0)) {
            continue;
        }
        if (pillar.returns.size() < params.min_return_observations) {
            continue;
        }
        std::vector<double> raw;
        raw.reserve(pillar.returns.size());
        for (const database::PillarReturn& entry : pillar.returns) {
            raw.push_back(entry.log_return);
            strip.returns_by_date[entry.as_of].emplace_back(tenor_it->second, entry.log_return);
            if (strip.history_start.empty() || entry.as_of < strip.history_start) {
                strip.history_start = entry.as_of;
            }
            if (entry.as_of > strip.history_end) {
                strip.history_end = entry.as_of;
            }
        }
        const double annualized =
                SampleStdDev(raw) * std::sqrt(static_cast<double>(params.vol_annualization_days));
        if (!(annualized > 0.0)) {
            continue;
        }
        strip.tenors.push_back(tenor_it->second);
        strip.volatilities.push_back(annualized);
    }

    if (strip.tenors.size() < 3U) {
        throw ValidationError("BuildGabillonCalibration: " + product_code + " has only " +
                              std::to_string(strip.tenors.size()) +
                              " usable pillar(s) on " + as_of +
                              "; separating a short from a long factor needs at least 3.");
    }
    return strip;
}

/// Project one session's pillar moves onto the two factor loadings.
///
/// A pillar \(\tau\) years out loads \(e^{-k\tau}\) on the short factor and the remainder
/// on the long one, so the strip is an over-determined view of two numbers. Least squares
/// on each session turns six noisy pillar returns into the two factor returns the model
/// actually has, which is what makes a cross-curve correlation meaningful.
[[nodiscard]] std::optional<FactorReturnPair> ExtractFactorReturns(
        const std::vector<std::pair<double, double>>& pillar_moves, const double mean_reversion) {
    if (pillar_moves.size() < 2U) {
        return std::nullopt;
    }
    double a11 = 0.0;
    double a12 = 0.0;
    double a22 = 0.0;
    double b1 = 0.0;
    double b2 = 0.0;
    for (const auto& [tenor, move] : pillar_moves) {
        const double decay = std::exp(-mean_reversion * tenor);
        const double carry = 1.0 - decay;
        a11 += decay * decay;
        a12 += decay * carry;
        a22 += carry * carry;
        b1 += decay * move;
        b2 += carry * move;
    }
    const double det = (a11 * a22) - (a12 * a12);
    if (std::abs(det) < 1.0e-12) {
        return std::nullopt;
    }
    return FactorReturnPair{
            .short_factor = ((a22 * b1) - (a12 * b2)) / det,
            .long_factor = ((a11 * b2) - (a12 * b1)) / det,
    };
}

/// Correlation across every factor of every curve.
///
/// Within a curve the volatility fit already produced a factor correlation beside the
/// vols it belongs with, so that one is kept rather than re-measured. Across curves only
/// the data can speak, so extracted factor returns are correlated on their common
/// sessions. Higham then repairs the seam between the two.
[[nodiscard]] std::vector<double> AssembleCorrelation(const std::vector<CurveCalibration>& curves,
                                                      const std::size_t min_observations) {
    const std::size_t n = curves.size() * 2U;
    std::vector<double> corr(n * n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        corr[(i * n) + i] = 1.0;
    }

    for (std::size_t p = 0; p < curves.size(); ++p) {
        const std::size_t ps = p * 2U;
        const std::size_t pl = ps + 1U;
        corr[(ps * n) + pl] = curves[p].vol_fit.factor_correlation;
        corr[(pl * n) + ps] = curves[p].vol_fit.factor_correlation;

        for (std::size_t q = p + 1U; q < curves.size(); ++q) {
            std::array<std::vector<double>, 4> series;
            for (const auto& [date, left] : curves[p].factor_returns_by_date) {
                const auto right_it = curves[q].factor_returns_by_date.find(date);
                if (right_it == curves[q].factor_returns_by_date.end()) {
                    continue;
                }
                series[0].push_back(left.short_factor);
                series[1].push_back(left.long_factor);
                series[2].push_back(right_it->second.short_factor);
                series[3].push_back(right_it->second.long_factor);
            }
            if (series[0].size() < min_observations) {
                throw ValidationError("BuildGabillonCalibration: " + curves[p].product_code + " and " +
                                      curves[q].product_code + " share only " +
                                      std::to_string(series[0].size()) +
                                      " session(s) of factor history; cross-curve correlation needs at least " +
                                      std::to_string(min_observations) + ".");
            }
            const std::size_t qs = q * 2U;
            for (std::size_t li = 0; li < 2U; ++li) {
                for (std::size_t ri = 0; ri < 2U; ++ri) {
                    const double rho = PearsonCorrelation(series.at(li), series.at(2U + ri));
                    corr[((ps + li) * n) + qs + ri] = rho;
                    corr[((qs + ri) * n) + ps + li] = rho;
                }
            }
        }
    }

    const quant::NearestCorrelationResult repaired = quant::NearestCorrelationHigham(corr, n);
    if (repaired.status != quant::NearestCorrelationStatus::kOk) {
        throw ValidationError(
                "BuildGabillonCalibration: assembled factor correlation could not be projected to a valid "
                "correlation matrix.");
    }
    return repaired.matrix;
}

[[nodiscard]] std::string ShortFactorId(const std::string& product_code) { return product_code + "_SHORT"; }
[[nodiscard]] std::string LongFactorId(const std::string& product_code) { return product_code + "_LONG"; }

void AppendCurveParams(const CurveCalibration& curve, std::vector<CalibrationParamWrite>& out) {
    const std::string& code = curve.product_code;
    const auto push = [&out, &code](const char* name, const double value) {
        out.push_back(CalibrationParamWrite{.factor_id = code, .param_name = name, .param_value = value});
    };
    push("mean_reversion", curve.vol_fit.mean_reversion);
    push("factor_correlation", curve.vol_fit.factor_correlation);
    push("vol_fit_rmse", curve.vol_fit.rmse);
    push("vol_fit_pillars", static_cast<double>(curve.num_vol_pillars));
    // The curve's own mean reversion is not the one that drives dynamics: it describes how
    // fast *levels* flatten out, while the volatility fit describes how fast *risk* hands
    // over from the short factor to the long one. Keeping both makes the gap visible.
    push("curve_mean_reversion", curve.curve_fit.mean_reversion);
    push("curve_short_level", curve.curve_fit.short_factor_level);
    push("curve_long_level", curve.curve_fit.long_factor_level);
    push("curve_log_rmse", curve.curve_fit.log_rmse);
    push("curve_num_contracts", static_cast<double>(curve.curve_fit.num_points));
    for (std::size_t h = 0; h < curve.curve_fit.seasonal.cos_coeffs.size(); ++h) {
        const std::string index = std::to_string(h + 1U);
        out.push_back(CalibrationParamWrite{.factor_id = code,
                                            .param_name = "seasonal_cos_" + index,
                                            .param_value = curve.curve_fit.seasonal.cos_coeffs[h]});
        out.push_back(CalibrationParamWrite{.factor_id = code,
                                            .param_name = "seasonal_sin_" + index,
                                            .param_value = curve.curve_fit.seasonal.sin_coeffs[h]});
    }
}

}  // namespace

GabillonCalibrationBuildStats BuildGabillonCalibration(const GabillonCalibrationBuildParams& params) {
    const std::optional<std::string_view> portfolio_id =
            params.scope_key == "ALL" ? std::nullopt : std::optional<std::string_view>{params.scope_key};

    const auto lifecycle = database::ApplyTradeLifecycleAsOf(params.database_file_path, params.as_of, portfolio_id);
    if (!lifecycle.expired_trade_ids.empty()) {
        Logger::NumInfo("calibrate-gabillon: expired {} matured trade(s) before as_of={}.",
                        lifecycle.expired_trade_ids.size(), params.as_of);
    }

    const std::vector<database::BookUnderlying> book =
            database::ListBookUnderlyings(params.database_file_path, std::string_view{"LIVE"}, portfolio_id);
    std::set<std::string> product_codes;
    for (const database::BookUnderlying& underlying : book) {
        if (underlying.asset_kind == "COMMODITY") {
            product_codes.insert(underlying.underlying_id);
        }
    }
    if (product_codes.empty()) {
        throw ValidationError("BuildGabillonCalibration: no commodity legs in scope_key=" + params.scope_key +
                              " on " + params.as_of + "; Gabillon calibrates commodity curves only.");
    }

    const schedule::Date as_of_date = schedule::ParseIsoDate(params.as_of);
    const std::string from_date =
            schedule::FormatIsoDate(schedule::AddCalendarDays(as_of_date, -params.lookback_calendar_days));

    std::vector<CurveCalibration> curves;
    std::string history_start;
    std::string history_end;
    for (const std::string& product_code : product_codes) {
        CurveCalibration curve;
        curve.product_code = product_code;
        curve.curve_fit = FitCurveShape(params.database_file_path, product_code, params.as_of,
                                        params.seasonal_harmonics);

        const PillarStrip strip =
                LoadPillarStrip(params.database_file_path, product_code, params.as_of, from_date, params);
        curve.num_vol_pillars = strip.tenors.size();
        curve.vol_fit = quant::FitGabillonVolatilities(strip.tenors, strip.volatilities);
        if (curve.vol_fit.status != quant::GabillonFitStatus::kOk) {
            throw ValidationError("BuildGabillonCalibration: could not fit the " + product_code +
                                  " volatility term structure on " + params.as_of + ".");
        }
        for (const auto& [date, moves] : strip.returns_by_date) {
            if (const std::optional<FactorReturnPair> pair =
                        ExtractFactorReturns(moves, curve.vol_fit.mean_reversion)) {
                curve.factor_returns_by_date.emplace(date, *pair);
            }
        }
        if (history_start.empty() || strip.history_start < history_start) {
            history_start = strip.history_start;
        }
        history_end = std::max(history_end, strip.history_end);
        curves.push_back(std::move(curve));
    }

    const std::vector<double> correlation = AssembleCorrelation(curves, params.min_return_observations);
    const std::size_t n = curves.size() * 2U;

    quant::CholeskyResult chol = quant::CholeskyDecompose(correlation, n);
    if (chol.status != quant::CholeskyStatus::kOk) {
        // Two factors on one curve can land at |ρ| ≈ 1 once the vol strip is long enough
        // for the long factor to be identified; Higham then returns a matrix that is PD
        // only to working precision, and a raw Cholesky rejects it. Ridge a copy so the
        // stored correlations stay inside [-1, 1].
        std::vector<double> ridged = correlation;
        constexpr double kRidge = 1.0e-8;
        for (std::size_t i = 0; i < n; ++i) {
            ridged[(i * n) + i] += kRidge;
        }
        chol = quant::CholeskyDecompose(ridged, n);
    }
    if (chol.status != quant::CholeskyStatus::kOk) {
        throw ValidationError("BuildGabillonCalibration: factor correlation is not positive definite.");
    }

    std::vector<CalibrationFactorWrite> factors;
    std::vector<CalibrationParamWrite> param_writes;
    for (std::size_t p = 0; p < curves.size(); ++p) {
        // No `factor_level`: these are state variables, not prices. The simulation anchors
        // each dated contract on its own observed settle, which reprices today's curve
        // exactly — a two-parameter base curve cannot, and a booked leg must not start
        // life at a fitted price.
        factors.push_back(CalibrationFactorWrite{.factor_index = static_cast<int>(p * 2U),
                                                 .factor_id = ShortFactorId(curves[p].product_code),
                                                 .factor_level = std::nullopt,
                                                 .volatility = curves[p].vol_fit.short_factor_vol});
        factors.push_back(CalibrationFactorWrite{.factor_index = static_cast<int>((p * 2U) + 1U),
                                                 .factor_id = LongFactorId(curves[p].product_code),
                                                 .factor_level = std::nullopt,
                                                 .volatility = curves[p].vol_fit.long_factor_vol});
        AppendCurveParams(curves[p], param_writes);
    }

    std::vector<CalibrationCorrelationWrite> correlation_writes;
    correlation_writes.reserve((n * (n + 1U)) / 2U);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i; j < n; ++j) {
            correlation_writes.push_back(CalibrationCorrelationWrite{
                    .factor_i = static_cast<int>(i),
                    .factor_j = static_cast<int>(j),
                    .rho = std::clamp(correlation[(i * n) + j], -1.0, 1.0)});
        }
    }

    std::vector<CalibrationCholeskyWrite> cholesky_writes;
    cholesky_writes.reserve((n * (n + 1U)) / 2U);
    for (std::size_t row = 0; row < n; ++row) {
        for (std::size_t col = 0; col <= row; ++col) {
            cholesky_writes.push_back(CalibrationCholeskyWrite{.row_i = static_cast<int>(row),
                                                               .col_j = static_cast<int>(col),
                                                               .l_value = chol.factor.lower[(row * n) + col]});
        }
    }

    std::size_t common_sessions = 0;
    for (const auto& [date, unused] : curves.front().factor_returns_by_date) {
        const bool everywhere = std::all_of(curves.begin(), curves.end(), [&date](const CurveCalibration& c) {
            return c.factor_returns_by_date.contains(date);
        });
        common_sessions += everywhere ? 1U : 0U;
    }

    CalibrationHeaderWrite header{};
    header.model = database::calibration_model::kGabillon2F;
    header.source = database::calibration_source::kFit;
    header.scope_key = params.scope_key;
    header.as_of = params.as_of;
    header.num_factors = static_cast<int>(factors.size());
    header.history_start = history_start;
    header.history_end = history_end;
    header.lookback_calendar_days = params.lookback_calendar_days;
    header.min_return_observations = static_cast<int>(params.min_return_observations);
    header.vol_annualization_days = params.vol_annualization_days;
    header.num_return_observations = static_cast<int>(common_sessions);
    header.batch_run_id = params.batch_run_id;
    header.remarks = "seasonal shape from the " + params.as_of +
                     " forward curve; dynamics from the pillar volatility term structure";

    SqliteCalibrationRepository repo(params.database_file_path);
    const std::int64_t calibration_id =
            repo.UpsertSnapshot(header, factors, correlation_writes, cholesky_writes, param_writes);

    GabillonCalibrationBuildStats stats{};
    stats.calibration_id = calibration_id;
    stats.num_factors = header.num_factors;
    stats.num_return_observations = common_sessions;
    for (const CurveCalibration& curve : curves) {
        double amplitude = 0.0;
        if (!curve.curve_fit.seasonal.cos_coeffs.empty()) {
            amplitude = std::hypot(curve.curve_fit.seasonal.cos_coeffs.front(),
                                   curve.curve_fit.seasonal.sin_coeffs.front());
        }
        stats.curves.push_back(GabillonCurveSummary{
                .product_code = curve.product_code,
                .mean_reversion = curve.vol_fit.mean_reversion,
                .short_factor_vol = curve.vol_fit.short_factor_vol,
                .long_factor_vol = curve.vol_fit.long_factor_vol,
                .factor_correlation = curve.vol_fit.factor_correlation,
                .vol_rmse = curve.vol_fit.rmse,
                .num_vol_pillars = curve.num_vol_pillars,
                .seasonal_amplitude = amplitude,
                .curve_log_rmse = curve.curve_fit.log_rmse,
                .num_curve_contracts = curve.curve_fit.num_points,
        });
    }
    return stats;
}

void PrintGabillonCalibrationUsageLines() {
    Logger::NumError(
            "  dev_main --calibrate-gabillon --as-of YYYY-MM-DD "
            "[--book PORTFOLIO_ID] [--lookback-days N] [--min-return-obs N] [--pillars N] [--harmonics N]\n"
            "    Two-factor Gabillon fit for the commodity curves in a LIVE book.\n"
            "    Seasonality is read off the whole forward curve on `--as-of`, where the annual cycle "
            "repeats across the deferred years; mean reversion and the two factor vols come from the "
            "term structure of pillar volatilities over `--lookback-days`.\n"
            "    Each curve becomes two factors, CL_SHORT / CL_LONG, replacing the CL_M1..CL_MN strip that "
            "GBM had to correlate pairwise.\n"
            "    Env defaults (CLI overrides): NUMERAIRE_CALIB_AS_OF, NUMERAIRE_CALIB_BOOK, "
            "NUMERAIRE_CALIB_LOOKBACK_DAYS, NUMERAIRE_CALIB_MIN_RETURN_OBS, "
            "NUMERAIRE_CALIB_VOL_ANNUALIZATION_DAYS, NUMERAIRE_CALIB_GABILLON_PILLARS, "
            "NUMERAIRE_CALIB_SEASONAL_HARMONICS.\n"
            "    Writes `calibration_snapshot` (model=gabillon_2f, source=fit) + factor / correlation / "
            "Cholesky / param child tables.");
}

int TryRunGabillonCalibration(const int argc, char** argv, const numeraire::utils::Config& cfg) {
    bool mode = false;
    std::string as_of;
    std::string book;
    int lookback_days = EnvInt("NUMERAIRE_CALIB_LOOKBACK_DAYS", 504);
    int min_return_obs = EnvInt("NUMERAIRE_CALIB_MIN_RETURN_OBS", 60);
    int vol_annualization_days = EnvInt("NUMERAIRE_CALIB_VOL_ANNUALIZATION_DAYS", 252);
    // Deeper than the GBM strip on purpose, and under its own name because the two mean
    // different things: a GBM pillar is a risk factor to be correlated pairwise, so more
    // of them cost dearly, while a Gabillon pillar is only an observation point for a
    // three-parameter vol curve. Stop at six and sigma_long describes years the fit never
    // saw — on gas it lands near 36% against a deferred market that moves at 10%.
    int commodity_pillars = EnvInt("NUMERAIRE_CALIB_GABILLON_PILLARS", 24);
    int seasonal_harmonics = EnvInt("NUMERAIRE_CALIB_SEASONAL_HARMONICS", 2);
    if (const std::optional<std::string> book_env = EnvNonEmptyString("NUMERAIRE_CALIB_BOOK")) {
        book = *book_env;
    }
    if (const std::optional<std::string> as_of_env = EnvNonEmptyString("NUMERAIRE_CALIB_AS_OF")) {
        as_of = *as_of_env;
    }

    const auto require_value = [argc, argv](int& i, const char* flag) -> const char* {
        if (i + 1 >= argc) {
            Logger::NumError("{} requires a value.", flag);
            return nullptr;
        }
        return argv[++i];
    };

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--calibrate-gabillon") == 0) {
            mode = true;
        } else if (std::strcmp(argv[i], "--as-of") == 0) {
            const char* value = require_value(i, "--as-of");
            if (value == nullptr) {
                return 1;
            }
            as_of = value;
        } else if (std::strcmp(argv[i], "--book") == 0) {
            const char* value = require_value(i, "--book");
            if (value == nullptr) {
                return 1;
            }
            book = value;
        } else if (std::strcmp(argv[i], "--lookback-days") == 0) {
            const char* value = require_value(i, "--lookback-days");
            if (value == nullptr) {
                return 1;
            }
            lookback_days = std::atoi(value);
        } else if (std::strcmp(argv[i], "--min-return-obs") == 0) {
            const char* value = require_value(i, "--min-return-obs");
            if (value == nullptr) {
                return 1;
            }
            min_return_obs = std::atoi(value);
        } else if (std::strcmp(argv[i], "--pillars") == 0) {
            const char* value = require_value(i, "--pillars");
            if (value == nullptr) {
                return 1;
            }
            commodity_pillars = std::atoi(value);
        } else if (std::strcmp(argv[i], "--harmonics") == 0) {
            const char* value = require_value(i, "--harmonics");
            if (value == nullptr) {
                return 1;
            }
            seasonal_harmonics = std::atoi(value);
        }
    }

    if (!mode) {
        return -1;
    }
    if (as_of.empty()) {
        Logger::NumError("--calibrate-gabillon requires --as-of YYYY-MM-DD or NUMERAIRE_CALIB_AS_OF.");
        PrintGabillonCalibrationUsageLines();
        return 1;
    }
    if (!LooksIsoDate(as_of)) {
        Logger::NumError("--as-of must be YYYY-MM-DD.");
        return 1;
    }
    if (lookback_days <= 0) {
        Logger::NumError("--lookback-days must be > 0.");
        return 1;
    }
    if (min_return_obs < 2) {
        Logger::NumError("--min-return-obs must be >= 2.");
        return 1;
    }
    if (vol_annualization_days <= 0) {
        Logger::NumError("NUMERAIRE_CALIB_VOL_ANNUALIZATION_DAYS must be > 0.");
        return 1;
    }
    if (commodity_pillars < 3) {
        Logger::NumError("--pillars must be >= 3: two factors cannot be separated from fewer maturities.");
        return 1;
    }
    if (seasonal_harmonics < 0) {
        Logger::NumError("--harmonics must be >= 0.");
        return 1;
    }

    const std::filesystem::path db_path = ResolveDatabasePath(cfg);
    database::BootstrapTradeDatabaseSchema(db_path, "sql/schema_v1.sql");

    GabillonCalibrationBuildParams params{};
    params.database_file_path = db_path.string();
    params.as_of = as_of;
    params.scope_key = book.empty() ? "ALL" : book;
    params.batch_run_id = "gabillon-2f-" + params.scope_key + "-" + as_of;
    params.lookback_calendar_days = lookback_days;
    params.min_return_observations = static_cast<std::size_t>(min_return_obs);
    params.vol_annualization_days = vol_annualization_days;
    params.commodity_pillars = commodity_pillars;
    params.seasonal_harmonics = seasonal_harmonics;

    Logger::NumInfo("calibrate-gabillon → SQLite {} scope_key={} as_of={} lookback_days={} harmonics={}.",
                    db_path.string(), params.scope_key, as_of, lookback_days, seasonal_harmonics);

    try {
        const GabillonCalibrationBuildStats stats = BuildGabillonCalibration(params);
        for (const GabillonCurveSummary& curve : stats.curves) {
            Logger::NumInfo(
                    "  {}: k={:.3f}/yr sigma_short={:.1f}% sigma_long={:.1f}% rho={:+.3f} "
                    "(vol RMSE {:.4f} on {} pillars); seasonal amplitude {:.4f} from {} contracts "
                    "(curve log RMSE {:.4f}).",
                    curve.product_code, curve.mean_reversion, 100.0 * curve.short_factor_vol,
                    100.0 * curve.long_factor_vol, curve.factor_correlation, curve.vol_rmse,
                    curve.num_vol_pillars, curve.seasonal_amplitude, curve.num_curve_contracts,
                    curve.curve_log_rmse);
        }
        Logger::NumInfo("calibrate-gabillon finished: calibration_id={} factors={} common_sessions={}.",
                        stats.calibration_id, stats.num_factors, stats.num_return_observations);
    } catch (const ValidationError& ex) {
        Logger::NumError("calibrate-gabillon failed: {}.", ex.what());
        return 1;
    }

    return 0;
}

}  // namespace numeraire::simulation
