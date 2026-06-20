#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <numeraire/database/historical_calibration_eod_read.hpp>
#include <numeraire/database/sqlite_schema.hpp>
#include <numeraire/database/trade_lifecycle.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/simulation/exposure_grid_config.hpp>
#include <numeraire/simulation/exposure_time_grid.hpp>
#include <numeraire/simulation/gbm_evolution.hpp>
#include <numeraire/simulation/historical_calibration_loader.hpp>
#include <numeraire/simulation/historical_gbm_simulate.hpp>
#include <numeraire/simulation/leg_path_pv_buffer.hpp>
#include <numeraire/simulation/path_pricer.hpp>
#include <numeraire/simulation/path_pricing_quotes.hpp>
#include <numeraire/simulation/random_engine.hpp>
#include <numeraire/simulation/scenario_buffer.hpp>
#include <numeraire/simulation/scenario_dump.hpp>
#include <numeraire/utils/config.hpp>
#include <numeraire/utils/database_path.hpp>
#include <numeraire/utils/exception.hpp>
#include <numeraire/pricers/pricer_factory.hpp>
#include <numeraire/enums/model_type.hpp>
#include <numeraire/utils/logger.hpp>
#include <optional>
#include <string>
#include <unordered_map>

namespace numeraire::simulation {
namespace {

using numeraire::database::TryLoadLatestHistoricalCalibrationEod;
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

[[nodiscard]] double EnvDouble(const char* key, const double default_value) {
    const char* raw = std::getenv(key);
    if (raw == nullptr || raw[0] == '\0') {
        return default_value;
    }
    char* end = nullptr;
    const double v = std::strtod(raw, &end);
    if (end == raw) {
        return default_value;
    }
    return v;
}

[[nodiscard]] std::optional<std::string> EnvNonEmptyString(const char* key) {
    const char* raw = std::getenv(key);
    if (raw == nullptr || raw[0] == '\0') {
        return std::nullopt;
    }
    return std::string{raw};
}

[[nodiscard]] int DefaultPathsFromConfig(const numeraire::utils::Config& cfg) {
    try {
        return cfg.RequireAt("pricing.monte_carlo.default_paths").get<int>();
    } catch (const numeraire::ConfigError&) {
        return 100000;
    }
}

[[nodiscard]] int DefaultSeedFromConfig(const numeraire::utils::Config& cfg) {
    try {
        return cfg.RequireAt("pricing.monte_carlo.default_seed").get<int>();
    } catch (const numeraire::ConfigError&) {
        return 42;
    }
}

[[nodiscard]] std::filesystem::path ResolveExposureGridConfigPath(const numeraire::utils::Config& cfg) {
    if (const std::optional<std::filesystem::path> from_defaults =
                ExposureGridConfigPathFromDefaults("configs/default.json");
        from_defaults.has_value()) {
        return *from_defaults;
    }
    static_cast<void>(cfg);
    return std::filesystem::path{"configs/simulation_exposure_grid.json"};
}

}  // namespace

void PrintHistoricalGbmSimulateUsageLines() {
    Logger::NumError(
            "  dev_main --simulate --as-of YYYY-MM-DD --book PORTFOLIO_ID [--paths N] [--seed N]\n"
            "    Multifactor GBM paths from latest `historical_calibration_*` snapshot with "
            "`as_of <= valuation_date` for the portfolio (`scope_key = portfolio_id`).\n"
            "    Env: NUMERAIRE_SIM_BOOK / NUMERAIRE_CALIB_BOOK, NUMERAIRE_DEV_AS_OF, "
            "NUMERAIRE_MC_PATHS, NUMERAIRE_MC_SEED, NUMERAIRE_DEV_RATE, NUMERAIRE_DEV_DIV_YIELD, "
            "NUMERAIRE_DEV_VOL, NUMERAIRE_DUMP_SCENARIOS, NUMERAIRE_DUMP_SCENARIOS_MAX_PATHS.\n"
            "    Optional: --price-paths reprices LIVE portfolio legs on every (step, path).");
}

int TryRunHistoricalGbmSimulate(const int argc, char** argv, const numeraire::utils::Config& cfg) {
    bool mode = false;
    bool price_paths = false;
    std::string as_of;
    std::string book;
    int num_paths = EnvInt("NUMERAIRE_MC_PATHS", DefaultPathsFromConfig(cfg));
    int seed = EnvInt("NUMERAIRE_MC_SEED", DefaultSeedFromConfig(cfg));

    if (const std::optional<std::string> book_env = EnvNonEmptyString("NUMERAIRE_SIM_BOOK")) {
        book = *book_env;
    } else if (const std::optional<std::string> calib_book = EnvNonEmptyString("NUMERAIRE_CALIB_BOOK")) {
        book = *calib_book;
    }

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--simulate") == 0) {
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
        } else if (std::strcmp(argv[i], "--paths") == 0) {
            if (i + 1 >= argc) {
                Logger::NumError("--paths requires a positive integer.");
                return 1;
            }
            num_paths = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--price-paths") == 0) {
            price_paths = true;
        } else if (std::strcmp(argv[i], "--seed") == 0) {
            if (i + 1 >= argc) {
                Logger::NumError("--seed requires an integer.");
                return 1;
            }
            seed = std::atoi(argv[++i]);
        }
    }

    if (!mode) {
        return -1;
    }

    if (as_of.empty()) {
        if (const std::optional<std::string> dev_as_of = EnvNonEmptyString("NUMERAIRE_DEV_AS_OF")) {
            as_of = *dev_as_of;
        } else if (const std::optional<std::string> calib_as_of = EnvNonEmptyString("NUMERAIRE_CALIB_AS_OF")) {
            as_of = *calib_as_of;
        }
    }
    if (as_of.empty()) {
        Logger::NumError("--simulate requires --as-of YYYY-MM-DD or NUMERAIRE_DEV_AS_OF.");
        PrintHistoricalGbmSimulateUsageLines();
        return 1;
    }
    if (!LooksIsoDate(as_of)) {
        Logger::NumError("--as-of must be YYYY-MM-DD.");
        return 1;
    }
    if (book.empty()) {
        Logger::NumError("--simulate requires --book PORTFOLIO_ID or NUMERAIRE_SIM_BOOK.");
        PrintHistoricalGbmSimulateUsageLines();
        return 1;
    }
    if (num_paths <= 0) {
        Logger::NumError("--paths must be > 0.");
        return 1;
    }

    const std::filesystem::path db_path = ResolveDatabasePath(cfg);
    database::BootstrapTradeDatabaseSchema(db_path, "sql/schema_v1.sql");

    const auto lifecycle = database::ApplyTradeLifecycleAsOf(db_path.string(), as_of, book);
    if (!lifecycle.expired_trade_ids.empty()) {
        Logger::NumInfo("simulate: expired {} matured trade(s) before as_of={}.",
                        lifecycle.expired_trade_ids.size(),
                        as_of);
    }

    const double risk_free_rate = EnvDouble("NUMERAIRE_DEV_RATE", 0.03);
    const double dividend_yield = EnvDouble("NUMERAIRE_DEV_DIV_YIELD", 0.0);
    const double flat_vol = EnvDouble("NUMERAIRE_DEV_VOL", 0.20);

    const std::optional<database::HistoricalCalibrationEodRead> calibration_read =
            TryLoadLatestHistoricalCalibrationEod(db_path.string(), book, as_of);
    if (!calibration_read.has_value()) {
        Logger::NumError(
                "simulate: no historical_calibration for scope_key={} with as_of <= {}. "
                "Run --calibrate-historical-gbm --book {} --as-of <month_start> first.",
                book,
                as_of,
                book);
        return 1;
    }

    const std::optional<MultiFactorGbmSpec> spec =
            TryLoadMultiFactorGbmSpecFromDatabase(db_path.string(), book, as_of, risk_free_rate, dividend_yield);
    if (!spec.has_value()) {
        Logger::NumError("simulate: failed to build MultiFactorGbmSpec for scope_key={}.", book);
        return 1;
    }

    const ExposureGridConfig grid_cfg = LoadExposureGridConfig(ResolveExposureGridConfigPath(cfg));
    const schedule::Date valuation_date = schedule::ParseIsoDate(as_of);
    const ExposureTimeGrid time_grid = BuildExposureTimeGrid(grid_cfg, valuation_date, std::nullopt);

    ScenarioBuffer buffer(spec->NumFactors(), time_grid.NumSteps(), static_cast<std::size_t>(num_paths));
    MersenneTwisterEngine engine(static_cast<std::uint64_t>(seed));
    EvolveMultiFactorGbm(buffer, time_grid, *spec, engine);

    const bool dumped = DumpMultiFactorScenarioPathsIfEnvSet(
            buffer, time_grid, std::span<const std::string>(calibration_read->factor_ids));
    const std::size_t terminal_step = time_grid.NumSteps() - 1;

    Logger::NumInfo(
            "simulate finished: valuation_as_of={} calibration_as_of={} calibration_id={} "
            "scope_key={} factors={} paths={} seed={} grid_steps={} rate={} div_yield={}.",
            as_of,
            calibration_read->as_of,
            calibration_read->calibration_id,
            book,
            spec->NumFactors(),
            num_paths,
            seed,
            time_grid.NumSteps(),
            risk_free_rate,
            dividend_yield);

    for (std::size_t factor = 0; factor < spec->NumFactors(); ++factor) {
        double sum = 0.0;
        for (std::size_t mc_path = 0; mc_path < buffer.NumPaths(); ++mc_path) {
            sum += buffer.At(factor, terminal_step, mc_path);
        }
        const double mean_terminal = sum / static_cast<double>(buffer.NumPaths());
        Logger::NumInfo("  factor[{}] {} spot0={:.4f} mean_terminal={:.4f} vol={:.4f}",
                        factor,
                        calibration_read->factor_ids[factor],
                        spec->spots[factor],
                        mean_terminal,
                        spec->volatilities[factor]);
    }

    if (dumped) {
        Logger::NumInfo(
                "simulate: multifactor scenario CSV written (NUMERAIRE_DUMP_SCENARIOS, all factors).");
    }

    if (price_paths) {
        const std::unordered_map<std::string, std::size_t> factor_by_underlying =
                BuildFactorIndexByUnderlying(calibration_read->factor_ids);
        const std::vector<PathPricingLegEntry> legs =
                LoadPathPricingLegsForPortfolio(db_path.string(), book, factor_by_underlying);

        const PathPricingQuotes quotes{
                .risk_free_rate = risk_free_rate,
                .dividend_yield = dividend_yield,
                .flat_implied_volatility = flat_vol,
        };
        auto pricer = pricers::PricerFactory::Make(numeraire::PricingEngineType::kAnalytic,
                                                   numeraire::ModelType::kBlackScholes);
        LegPathPvBuffer leg_pv(legs.size(), time_grid.NumSteps(), buffer.NumPaths());
        std::vector<std::string> leg_ids;
        PricePortfolioAlongPaths(buffer, time_grid,
                                 std::span<const std::string>(calibration_read->factor_ids), legs,
                                 quotes, *pricer, leg_pv, leg_ids);

        Logger::NumInfo("simulate: path pricing finished legs={} steps={} paths={}.",
                        leg_ids.size(),
                        time_grid.NumSteps(),
                        buffer.NumPaths());

        for (std::size_t leg_index = 0; leg_index < leg_ids.size(); ++leg_index) {
            double sum_t0 = 0.0;
            double sum_terminal = 0.0;
            for (std::size_t mc_path = 0; mc_path < buffer.NumPaths(); ++mc_path) {
                sum_t0 += leg_pv.At(leg_index, 0, mc_path);
                sum_terminal += leg_pv.At(leg_index, terminal_step, mc_path);
            }
            const double mean_t0 = sum_t0 / static_cast<double>(buffer.NumPaths());
            const double mean_terminal = sum_terminal / static_cast<double>(buffer.NumPaths());
            Logger::NumInfo("  leg[{}] {} mean_pv_t0={:.4f} mean_pv_terminal={:.4f}",
                            leg_index,
                            leg_ids[leg_index],
                            mean_t0,
                            mean_terminal);
        }
    }

    return 0;
}

}  // namespace numeraire::simulation
