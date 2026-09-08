#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <numeraire/database/calibration_types.hpp>
#include <numeraire/database/sqlite_calibration_repository.hpp>
#include <numeraire/database/sqlite_schema.hpp>
#include <numeraire/database/trade_lifecycle.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/schedule/format_iso_date.hpp>
#include <numeraire/simulation/historical_calibration_eod_builder.hpp>
#include <numeraire/simulation/historical_calibrator.hpp>
#include <numeraire/utils/config.hpp>
#include <numeraire/utils/database_path.hpp>
#include <numeraire/utils/exception.hpp>
#include <numeraire/utils/logger.hpp>
#include <optional>
#include <string>
#include <vector>

namespace numeraire::simulation {
namespace {

using numeraire::database::CalibrationCholeskyWrite;
using numeraire::database::CalibrationCorrelationWrite;
using numeraire::database::CalibrationFactorWrite;
using numeraire::database::CalibrationHeaderWrite;
using numeraire::database::SqliteCalibrationRepository;
using numeraire::utils::Logger;
using numeraire::utils::ResolveDatabasePath;

[[nodiscard]] bool LooksIsoDate(const std::string& s) {
    return s.size() == 10U && s[4] == '-' && s[7] == '-';
}

[[nodiscard]] int EnvInt(const char* key, const int default_value) {
    const char* raw = std::getenv(key);
    if (raw == nullptr || raw[0] == '\0') {
        return default_value;
    }
    char* end = nullptr;
    const long v = std::strtol(raw, &end, 10);
    if (end == raw) {
        return default_value;
    }
    return static_cast<int>(v);
}

[[nodiscard]] std::optional<std::string> EnvNonEmptyString(const char* key) {
    const char* raw = std::getenv(key);
    if (raw == nullptr || raw[0] == '\0') {
        return std::nullopt;
    }
    return std::string{raw};
}

[[nodiscard]] std::vector<CalibrationFactorWrite> ToFactorWrites(const HistoricalCalibrationResult& result) {
    std::vector<CalibrationFactorWrite> out;
    out.reserve(result.factor_ids.size());
    for (std::size_t i = 0; i < result.factor_ids.size(); ++i) {
        out.push_back(CalibrationFactorWrite{
                .factor_index = static_cast<int>(i),
                .factor_id = result.factor_ids[i],
                .factor_level = result.spots_as_of[i],
                .volatility = result.volatilities[i],
        });
    }
    return out;
}

[[nodiscard]] std::vector<CalibrationCorrelationWrite> ToCorrelationWrites(
        const HistoricalCalibrationResult& result) {
    const std::size_t n = result.factor_ids.size();
    std::vector<CalibrationCorrelationWrite> out;
    out.reserve((n * (n + 1)) / 2);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i; j < n; ++j) {
            out.push_back(CalibrationCorrelationWrite{
                    .factor_i = static_cast<int>(i),
                    .factor_j = static_cast<int>(j),
                    .rho = result.correlation[(i * n) + j],
            });
        }
    }
    return out;
}

[[nodiscard]] std::vector<CalibrationCholeskyWrite> ToCholeskyWrites(const HistoricalCalibrationResult& result) {
    const std::size_t n = result.cholesky.n;
    std::vector<CalibrationCholeskyWrite> out;
    out.reserve((n * (n + 1)) / 2);
    for (std::size_t row = 0; row < n; ++row) {
        for (std::size_t col = 0; col <= row; ++col) {
            out.push_back(CalibrationCholeskyWrite{
                    .row_i = static_cast<int>(row),
                    .col_j = static_cast<int>(col),
                    .l_value = result.cholesky.lower[(row * n) + col],
            });
        }
    }
    return out;
}

}  // namespace

HistoricalCalibrationBuildStats BuildHistoricalCalibrationEod(const HistoricalCalibrationBuildParams& params) {
    HistoricalCalibratorConfig config{};
    config.as_of = schedule::ParseIsoDate(params.as_of);
    config.lookback_calendar_days = params.lookback_calendar_days;
    config.min_return_observations = params.min_return_observations;
    config.vol_annualization_days = params.vol_annualization_days;
    config.adjusted = params.adjusted;
    config.commodity_pillars = params.commodity_pillars;

    const std::optional<std::string_view> portfolio_id =
            params.scope_key == "ALL" ? std::nullopt : std::optional<std::string_view>{params.scope_key};

    const auto lifecycle = database::ApplyTradeLifecycleAsOf(params.database_file_path, params.as_of, portfolio_id);
    if (!lifecycle.expired_trade_ids.empty()) {
        Logger::NumInfo("calibrate-historical-gbm: expired {} matured trade(s) before as_of={}.",
                        lifecycle.expired_trade_ids.size(),
                        params.as_of);
    }

    const HistoricalCalibrationResult result =
            CalibrateBookFromDatabase(params.database_file_path, config, portfolio_id);

    CalibrationHeaderWrite header{};
    header.model = database::calibration_model::kGbm;
    header.source = database::calibration_source::kHistorical;
    header.scope_key = params.scope_key;
    header.as_of = params.as_of;
    header.num_factors = static_cast<int>(result.factor_ids.size());
    header.history_start = schedule::FormatIsoDate(result.history_start);
    header.history_end = schedule::FormatIsoDate(result.history_end);
    header.lookback_calendar_days = params.lookback_calendar_days;
    header.min_return_observations = static_cast<int>(params.min_return_observations);
    header.vol_annualization_days = params.vol_annualization_days;
    header.eod_adjusted = params.adjusted;
    header.num_return_observations = static_cast<int>(result.num_return_observations);
    header.batch_run_id = params.batch_run_id;

    SqliteCalibrationRepository repo(params.database_file_path);
    const long calibration_id = repo.UpsertSnapshot(header,
                                                   ToFactorWrites(result),
                                                   ToCorrelationWrites(result),
                                                   ToCholeskyWrites(result));

    HistoricalCalibrationBuildStats stats{};
    stats.calibration_id = calibration_id;
    stats.num_factors = header.num_factors;
    stats.num_return_observations = result.num_return_observations;
    return stats;
}

void PrintHistoricalCalibrationEodBuildUsageLines() {
    Logger::NumError(
            "  dev_main --calibrate-historical-gbm --as-of YYYY-MM-DD "
            "[--book PORTFOLIO_ID] [--lookback-days N] [--min-return-obs N] [--pillars N]\n"
            "    Historical EOD GBM calibration (vol + correlation + Cholesky) for LIVE book legs.\n"
            "    `--book` scopes factors to one `trades.portfolio_id`; omit for all portfolios (`scope_key=ALL`).\n"
            "    Each equity underlying is one factor; each commodity curve expands into constant-maturity\n"
            "    pillars CL_M1..CL_MN (`--pillars`, default 6) built from roll-adjusted futures returns.\n"
            "    Env defaults (CLI overrides): NUMERAIRE_CALIB_AS_OF, NUMERAIRE_CALIB_BOOK, "
            "NUMERAIRE_CALIB_LOOKBACK_DAYS, NUMERAIRE_CALIB_MIN_RETURN_OBS, "
            "NUMERAIRE_CALIB_VOL_ANNUALIZATION_DAYS, NUMERAIRE_CALIB_EOD_ADJUSTED, "
            "NUMERAIRE_CALIB_COMMODITY_PILLARS.\n"
            "    Writes `calibration_snapshot` (model=gbm, source=historical) + factor / correlation / "
            "Cholesky child tables.");
}

int TryRunHistoricalCalibrationEodBuild(const int argc, char** argv, const numeraire::utils::Config& cfg) {
    bool mode = false;
    std::string as_of;
    std::string book;
    int lookback_days = EnvInt("NUMERAIRE_CALIB_LOOKBACK_DAYS", 504);
    int min_return_obs = EnvInt("NUMERAIRE_CALIB_MIN_RETURN_OBS", 60);
    int vol_annualization_days = EnvInt("NUMERAIRE_CALIB_VOL_ANNUALIZATION_DAYS", 252);
    int eod_adjusted = EnvInt("NUMERAIRE_CALIB_EOD_ADJUSTED", 1);
    int commodity_pillars = EnvInt("NUMERAIRE_CALIB_COMMODITY_PILLARS", 6);
    if (const std::optional<std::string> book_env = EnvNonEmptyString("NUMERAIRE_CALIB_BOOK")) {
        book = *book_env;
    }

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--calibrate-historical-gbm") == 0) {
            mode = true;
        } else if (std::strcmp(argv[i], "--as-of") == 0) {
            if (i + 1 >= argc) {
                Logger::NumError("--as-of requires YYYY-MM-DD.");
                return 1;
            }
            as_of = argv[++i];
        } else if (std::strcmp(argv[i], "--book") == 0) {
            if (i + 1 >= argc) {
                Logger::NumError("--book requires a portfolio_id (e.g. BOOK_1).");
                return 1;
            }
            book = argv[++i];
        } else if (std::strcmp(argv[i], "--lookback-days") == 0) {
            if (i + 1 >= argc) {
                Logger::NumError("--lookback-days requires a positive integer.");
                return 1;
            }
            lookback_days = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--min-return-obs") == 0) {
            if (i + 1 >= argc) {
                Logger::NumError("--min-return-obs requires a positive integer.");
                return 1;
            }
            min_return_obs = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--pillars") == 0) {
            if (i + 1 >= argc) {
                Logger::NumError("--pillars requires a positive integer.");
                return 1;
            }
            commodity_pillars = std::atoi(argv[++i]);
        }
    }

    if (!mode) {
        return -1;
    }

    if (as_of.empty()) {
        if (const std::optional<std::string> as_of_env = EnvNonEmptyString("NUMERAIRE_CALIB_AS_OF")) {
            as_of = *as_of_env;
        }
    }
    if (as_of.empty()) {
        Logger::NumError(
                "--calibrate-historical-gbm requires --as-of YYYY-MM-DD or NUMERAIRE_CALIB_AS_OF.");
        PrintHistoricalCalibrationEodBuildUsageLines();
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
    if (eod_adjusted != 0 && eod_adjusted != 1) {
        Logger::NumError("NUMERAIRE_CALIB_EOD_ADJUSTED must be 0 or 1.");
        return 1;
    }
    if (commodity_pillars <= 0) {
        Logger::NumError("--pillars must be > 0.");
        return 1;
    }

    const std::filesystem::path db_path = ResolveDatabasePath(cfg);
    database::BootstrapTradeDatabaseSchema(db_path, "sql/schema_v1.sql");

    HistoricalCalibrationBuildParams params{};
    params.database_file_path = db_path.string();
    params.as_of = as_of;
    params.scope_key = book.empty() ? "ALL" : book;
    params.batch_run_id = "historical-gbm-" + params.scope_key + "-" + as_of;
    params.lookback_calendar_days = lookback_days;
    params.min_return_observations = static_cast<std::size_t>(min_return_obs);
    params.vol_annualization_days = vol_annualization_days;
    params.adjusted = eod_adjusted;
    params.commodity_pillars = commodity_pillars;

    Logger::NumInfo("calibrate-historical-gbm → SQLite {} scope_key={} as_of={} lookback_days={}.",
                    db_path.string(),
                    params.scope_key,
                    as_of,
                    lookback_days);

    try {
        const HistoricalCalibrationBuildStats stats = BuildHistoricalCalibrationEod(params);
        Logger::NumInfo(
                "calibrate-historical-gbm finished: calibration_id={} factors={} return_obs={}.",
                stats.calibration_id,
                stats.num_factors,
                stats.num_return_observations);
    } catch (const ValidationError& ex) {
        Logger::NumError("calibrate-historical-gbm failed: {}.", ex.what());
        return 1;
    }

    return 0;
}

}  // namespace numeraire::simulation
