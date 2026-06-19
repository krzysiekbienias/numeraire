#include <cstring>
#include <filesystem>
#include <numeraire/database/historical_calibration_types.hpp>
#include <numeraire/database/sqlite_historical_calibration_repository.hpp>
#include <numeraire/database/sqlite_schema.hpp>
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

using numeraire::database::HistoricalCalibrationCholeskyWrite;
using numeraire::database::HistoricalCalibrationCorrelationWrite;
using numeraire::database::HistoricalCalibrationFactorWrite;
using numeraire::database::HistoricalCalibrationHeaderWrite;
using numeraire::database::SqliteHistoricalCalibrationRepository;
using numeraire::utils::Logger;
using numeraire::utils::ResolveDatabasePath;

[[nodiscard]] bool LooksIsoDate(const std::string& s) {
    return s.size() == 10U && s[4] == '-' && s[7] == '-';
}

[[nodiscard]] std::vector<HistoricalCalibrationFactorWrite> ToFactorWrites(
        const HistoricalCalibrationResult& result) {
    std::vector<HistoricalCalibrationFactorWrite> out;
    out.reserve(result.factor_ids.size());
    for (std::size_t i = 0; i < result.factor_ids.size(); ++i) {
        out.push_back(HistoricalCalibrationFactorWrite{
                .factor_index = static_cast<int>(i),
                .underlying_id = result.factor_ids[i],
                .spot_as_of = result.spots_as_of[i],
                .volatility = result.volatilities[i],
        });
    }
    return out;
}

[[nodiscard]] std::vector<HistoricalCalibrationCorrelationWrite> ToCorrelationWrites(
        const HistoricalCalibrationResult& result) {
    const std::size_t n = result.factor_ids.size();
    std::vector<HistoricalCalibrationCorrelationWrite> out;
    out.reserve((n * (n + 1)) / 2);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i; j < n; ++j) {
            out.push_back(HistoricalCalibrationCorrelationWrite{
                    .factor_i = static_cast<int>(i),
                    .factor_j = static_cast<int>(j),
                    .rho = result.correlation[(i * n) + j],
            });
        }
    }
    return out;
}

[[nodiscard]] std::vector<HistoricalCalibrationCholeskyWrite> ToCholeskyWrites(
        const HistoricalCalibrationResult& result) {
    const std::size_t n = result.cholesky.n;
    std::vector<HistoricalCalibrationCholeskyWrite> out;
    out.reserve((n * (n + 1)) / 2);
    for (std::size_t row = 0; row < n; ++row) {
        for (std::size_t col = 0; col <= row; ++col) {
            out.push_back(HistoricalCalibrationCholeskyWrite{
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

    const std::optional<std::string_view> portfolio_id =
            params.scope_key == "ALL" ? std::nullopt : std::optional<std::string_view>{params.scope_key};

    const HistoricalCalibrationResult result =
            CalibrateBookFromDatabase(params.database_file_path, config, portfolio_id);

    HistoricalCalibrationHeaderWrite header{};
    header.scope_key = params.scope_key;
    header.as_of = params.as_of;
    header.history_start = schedule::FormatIsoDate(result.history_start);
    header.history_end = schedule::FormatIsoDate(result.history_end);
    header.lookback_calendar_days = params.lookback_calendar_days;
    header.min_return_observations = static_cast<int>(params.min_return_observations);
    header.vol_annualization_days = params.vol_annualization_days;
    header.eod_adjusted = params.adjusted;
    header.num_factors = static_cast<int>(result.factor_ids.size());
    header.num_return_observations = static_cast<int>(result.num_return_observations);
    header.batch_run_id = params.batch_run_id;

    SqliteHistoricalCalibrationRepository repo(params.database_file_path);
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
            "[--book PORTFOLIO_ID] [--lookback-days N] [--min-return-obs N]\n"
            "    Historical EOD GBM calibration (vol + correlation + Cholesky) for LIVE book legs.\n"
            "    `--book` scopes factors to one `trades.portfolio_id`; omit for all portfolios (`scope_key=ALL`).\n"
            "    Writes `historical_calibration` + factor / correlation / Cholesky child tables.");
}

int TryRunHistoricalCalibrationEodBuild(const int argc, char** argv, const numeraire::utils::Config& cfg) {
    bool mode = false;
    std::string as_of;
    std::string book;
    int lookback_days = 504;
    int min_return_obs = 60;

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
        }
    }

    if (!mode) {
        return -1;
    }

    if (as_of.empty()) {
        Logger::NumError("--calibrate-historical-gbm requires --as-of YYYY-MM-DD.");
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

    const std::filesystem::path db_path = ResolveDatabasePath(cfg);
    database::BootstrapTradeDatabaseSchema(db_path, "sql/schema_v1.sql");

    HistoricalCalibrationBuildParams params{};
    params.database_file_path = db_path.string();
    params.as_of = as_of;
    params.scope_key = book.empty() ? "ALL" : book;
    params.batch_run_id = "historical-gbm-" + params.scope_key + "-" + as_of;
    params.lookback_calendar_days = lookback_days;
    params.min_return_observations = static_cast<std::size_t>(min_return_obs);

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
