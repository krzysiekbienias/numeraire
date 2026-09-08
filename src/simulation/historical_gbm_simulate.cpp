#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <numeraire/database/calibration_snapshot_read.hpp>
#include <numeraire/database/sqlite_schema.hpp>
#include <numeraire/database/sqlite_trade_leg_exposure_repository.hpp>
#include <numeraire/database/sqlite_trade_leg_mtm_repository.hpp>
#include <numeraire/database/trade_lifecycle.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/simulation/exposure_grid_config.hpp>
#include <numeraire/simulation/exposure_metrics.hpp>
#include <numeraire/simulation/exposure_time_grid.hpp>
#include <numeraire/simulation/gabillon_evolution.hpp>
#include <numeraire/simulation/gabillon_spec.hpp>
#include <numeraire/simulation/gbm_evolution.hpp>
#include <numeraire/simulation/historical_calibration_loader.hpp>
#include <numeraire/simulation/historical_gbm_simulate.hpp>
#include <numeraire/simulation/leg_exposure_dump.hpp>
#include <numeraire/simulation/leg_path_pv_buffer.hpp>
#include <numeraire/simulation/path_pricer.hpp>
#include <numeraire/simulation/path_pricing_market_config.hpp>
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
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace numeraire::simulation {
namespace {

using numeraire::database::TryLoadLatestCalibrationSnapshot;
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

[[nodiscard]] std::string DefaultDiscountCurveId() {
    if (const std::optional<std::string> from_env = EnvNonEmptyString("NUMERAIRE_DEV_DISCOUNT_CURVE_ID")) {
        return *from_env;
    }
    return "USD_TREASURY_PAR_FRED";
}

[[nodiscard]] bool EnvFlagEnabled(const char* key) {
    const char* raw = std::getenv(key);
    return raw != nullptr && raw[0] != '\0' && std::strcmp(raw, "0") != 0;
}

[[nodiscard]] std::string MakeExposureBatchRunId() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    gmtime_s(&tm_buf, &t);
#else
    gmtime_r(&t, &tm_buf);
#endif
    std::array<char, 32> buf{};
    if (std::strftime(buf.data(), buf.size(), "exposure-%Y%m%dT%H%M%SZ", &tm_buf) == 0U) {
        return "exposure-unknown";
    }
    return std::string(buf.data());
}

}  // namespace

void PrintHistoricalGbmSimulateUsageLines() {
    Logger::NumError(
            "  dev_main --simulate --as-of YYYY-MM-DD --book PORTFOLIO_ID [--model gbm|gabillon] "
            "[--paths N] [--seed N]\n"
            "    Multifactor GBM paths from latest `calibration_snapshot` (model=gbm, source=historical) with "
            "`as_of <= valuation_date` for the portfolio (`scope_key = portfolio_id`).\n"
            "    `--model gabillon` instead reads a `gabillon_2f`/`fit` snapshot and evolves each dated "
            "futures contract off its own settle as a driftless martingale, so today's curve — seasonal "
            "humps included — is reproduced exactly and only volatility decays with maturity.\n"
            "    Env: NUMERAIRE_SIM_MODEL, NUMERAIRE_SIM_BOOK / NUMERAIRE_CALIB_BOOK, NUMERAIRE_DEV_AS_OF, "
            "NUMERAIRE_MC_PATHS, NUMERAIRE_MC_SEED, NUMERAIRE_DEV_RATE, NUMERAIRE_DEV_DIV_YIELD, "
            "NUMERAIRE_DEV_VOL, NUMERAIRE_DEV_DISCOUNT_CURVE_ID, "
            "NUMERAIRE_DUMP_SCENARIOS, NUMERAIRE_DUMP_SCENARIOS_MAX_PATHS, "
            "NUMERAIRE_DUMP_LEG_EXPOSURE, NUMERAIRE_DUMP_LEG_EXPOSURE_MAX_PATHS, "
            "NUMERAIRE_PERSIST_EXPOSURE.\n"
            "    Optional: --price-paths reprices LIVE legs (IV+rate from DB @ as_of); "
            "--persist-exposure writes EE/PFE to trade_leg_exposure_eod only when every LIVE "
            "leg in the book already has an official FO MTM row on that as_of.");
}

int TryRunHistoricalGbmSimulate(const int argc, char** argv, const numeraire::utils::Config& cfg) {
    bool mode = false;
    bool price_paths = false;
    bool persist_exposure = EnvFlagEnabled("NUMERAIRE_PERSIST_EXPOSURE");
    std::string as_of;
    std::string book;
    std::string model = EnvNonEmptyString("NUMERAIRE_SIM_MODEL").value_or("gbm");
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
        } else if (std::strcmp(argv[i], "--model") == 0) {
            if (i + 1 >= argc) {
                Logger::NumError("--model requires gbm or gabillon.");
                return 1;
            }
            model = argv[++i];
        } else if (std::strcmp(argv[i], "--price-paths") == 0) {
            price_paths = true;
        } else if (std::strcmp(argv[i], "--persist-exposure") == 0) {
            persist_exposure = true;
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
    if (model != "gbm" && model != "gabillon") {
        Logger::NumError("--model must be gbm or gabillon (got \"{}\").", model);
        return 1;
    }
    const bool use_gabillon = model == "gabillon";

    const std::filesystem::path db_path = ResolveDatabasePath(cfg);
    database::BootstrapTradeDatabaseSchema(db_path, "sql/schema_v1.sql");

    const auto lifecycle = database::ApplyTradeLifecycleAsOf(db_path.string(), as_of, book);
    if (!lifecycle.expired_trade_ids.empty()) {
        Logger::NumInfo("simulate: expired {} matured trade(s) before as_of={}.",
                        lifecycle.expired_trade_ids.size(),
                        as_of);
    }

    if (persist_exposure && price_paths) {
        try {
            const database::SqliteTradeLegMtmRepository mtm_repo(db_path.string());
            const std::vector<std::string> missing = mtm_repo.LiveLegsMissingOfficialMtm(book, as_of);
            if (!missing.empty()) {
                std::ostringstream oss;
                for (std::size_t i = 0; i < missing.size(); ++i) {
                    if (i > 0) {
                        oss << ',';
                    }
                    oss << missing[i];
                }
                Logger::NumError(
                        "persist-exposure requires official FO MTM for every LIVE leg in book={} as_of={}. "
                        "Missing mark(s): {}. Run --as-of MTM first (holiday / missing settle → no exposure).",
                        book, as_of, oss.str());
                return 1;
            }
        } catch (const std::exception& e) {
            Logger::NumError("persist-exposure MTM gate failed: {}", e.what());
            return 1;
        }
    }

    const double risk_free_rate = EnvDouble("NUMERAIRE_DEV_RATE", 0.03);
    const double dividend_yield = EnvDouble("NUMERAIRE_DEV_DIV_YIELD", 0.0);
    const double flat_vol = EnvDouble("NUMERAIRE_DEV_VOL", 0.20);

    const ExposureGridConfig grid_cfg = LoadExposureGridConfig(ResolveExposureGridConfigPath(cfg));
    const schedule::Date valuation_date = schedule::ParseIsoDate(as_of);
    const ExposureTimeGrid time_grid = BuildExposureTimeGrid(grid_cfg, valuation_date, std::nullopt);
    const std::size_t terminal_step = time_grid.NumSteps() - 1;

    // Factor identity differs by model: GBM carries constant-maturity pillars, Gabillon
    // carries the dated contracts themselves. Everything downstream keys off these names.
    std::vector<std::string> factor_ids;
    std::vector<double> initial_levels;
    std::int64_t calibration_id = 0;
    std::string calibration_as_of;
    std::optional<GabillonSimulationSpec> gabillon_spec;
    std::optional<MultiFactorGbmSpec> gbm_spec;

    if (use_gabillon) {
        gabillon_spec = TryLoadGabillonSpecFromDatabase(db_path.string(), book, as_of);
        if (!gabillon_spec.has_value()) {
            Logger::NumError(
                    "simulate: no gabillon_2f/fit calibration snapshot for scope_key={} with as_of <= {}. "
                    "Run --calibrate-gabillon --book {} --as-of <session> first.",
                    book, as_of, book);
            return 1;
        }
        for (const GabillonContract& contract : gabillon_spec->contracts) {
            factor_ids.push_back(contract.contract_ticker);
            initial_levels.push_back(contract.anchor_price);
        }
        calibration_id = gabillon_spec->calibration_id;
        calibration_as_of = gabillon_spec->calibration_as_of;
    } else {
        const std::optional<database::CalibrationSnapshotRead> calibration_read =
                TryLoadLatestCalibrationSnapshot(db_path.string(), book, as_of);
        if (!calibration_read.has_value()) {
            Logger::NumError(
                    "simulate: no gbm/historical calibration snapshot for scope_key={} with as_of <= {}. "
                    "Run --calibrate-historical-gbm --book {} --as-of <month_start> first.",
                    book,
                    as_of,
                    book);
            return 1;
        }
        gbm_spec = TryLoadMultiFactorGbmSpecFromDatabase(db_path.string(), book, as_of, risk_free_rate,
                                                         dividend_yield);
        if (!gbm_spec.has_value()) {
            Logger::NumError("simulate: failed to build MultiFactorGbmSpec for scope_key={}.", book);
            return 1;
        }
        factor_ids = calibration_read->factor_ids;
        initial_levels = gbm_spec->spots;
        calibration_id = calibration_read->calibration_id;
        calibration_as_of = calibration_read->as_of;
    }

    const std::size_t num_factors = factor_ids.size();
    ScenarioBuffer buffer(num_factors, time_grid.NumSteps(), static_cast<std::size_t>(num_paths));
    MersenneTwisterEngine engine(static_cast<std::uint64_t>(seed));
    if (use_gabillon) {
        EvolveGabillonCurves(buffer, time_grid, *gabillon_spec, engine);
    } else {
        EvolveMultiFactorGbm(buffer, time_grid, *gbm_spec, engine);
    }

    const bool dumped =
            DumpMultiFactorScenarioPathsIfEnvSet(buffer, time_grid, std::span<const std::string>(factor_ids));

    Logger::NumInfo(
            "simulate finished: model={} valuation_as_of={} calibration_as_of={} calibration_id={} "
            "scope_key={} factors={} paths={} seed={} grid_steps={} rate={} div_yield={}.",
            use_gabillon ? "gabillon_2f" : "gbm",
            as_of,
            calibration_as_of,
            calibration_id,
            book,
            num_factors,
            num_paths,
            seed,
            time_grid.NumSteps(),
            risk_free_rate,
            dividend_yield);

    for (std::size_t factor = 0; factor < num_factors; ++factor) {
        double sum = 0.0;
        for (std::size_t mc_path = 0; mc_path < buffer.NumPaths(); ++mc_path) {
            sum += buffer.At(factor, terminal_step, mc_path);
        }
        const double mean_terminal = sum / static_cast<double>(buffer.NumPaths());
        // Futures are martingales, so for Gabillon the mean terminal should sit on the
        // anchor; drifting off it means the Ito correction and the shocks disagree.
        Logger::NumInfo("  factor[{}] {} level0={:.4f} mean_terminal={:.4f} drift={:+.3f}%",
                        factor,
                        factor_ids[factor],
                        initial_levels[factor],
                        mean_terminal,
                        100.0 * ((mean_terminal / initial_levels[factor]) - 1.0));
    }

    if (dumped) {
        Logger::NumInfo(
                "simulate: multifactor scenario CSV written (NUMERAIRE_DUMP_SCENARIOS, all {} paths).",
                buffer.NumPaths());
    }

    if (price_paths) {
        const std::unordered_map<std::string, std::size_t> factor_by_underlying =
                BuildFactorIndexByUnderlying(factor_ids);
        const std::vector<PathPricingLegEntry> legs =
                LoadPathPricingLegsForPortfolio(db_path.string(), book, factor_by_underlying);

        const PathPricingQuotes flat_fallbacks{
                .risk_free_rate = risk_free_rate,
                .dividend_yield = dividend_yield,
                .flat_implied_volatility = flat_vol,
        };
        const PathPricingMarketConfig market_config =
                LoadPathPricingMarketConfig(db_path.string(), std::span<const std::string>(factor_ids), as_of,
                                            DefaultDiscountCurveId(), flat_fallbacks);

        // Under Gabillon the dated contracts are factors in their own right, so there is
        // nothing to interpolate. A pillar calibration needs each contract placed on the
        // strip at every grid node. The strip is the one the book was calibrated to:
        // occupancy is the latest quoted session on or before that snapshot, so later
        // valuation days reuse it until the book is recalibrated. The factor count is a
        // safe upper bound on pillars per curve; the resolver keeps only those actually
        // in the calibrated factor set.
        std::optional<CommodityCurveResolver> commodity_curves;
        if (!use_gabillon) {
            commodity_curves = BuildCommodityCurveResolver(db_path.string(), CollectCommodityContracts(legs),
                                                           factor_by_underlying, time_grid,
                                                           schedule::ParseIsoDate(calibration_as_of),
                                                           static_cast<int>(factor_ids.size()));
        }

        auto pricer = pricers::PricerFactory::Make(numeraire::PricingEngineType::kAnalytic,
                                                   numeraire::ModelType::kBlackScholes);
        LegPathPvBuffer leg_pv(legs.size(), time_grid.NumSteps(), buffer.NumPaths());
        std::vector<std::string> leg_ids;
        PricePortfolioAlongPaths(buffer, time_grid, std::span<const std::string>(factor_ids), legs,
                                 market_config, *pricer, leg_pv, leg_ids,
                                 commodity_curves.has_value() ? &*commodity_curves : nullptr);

        Logger::NumInfo("simulate: path pricing finished legs={} steps={} paths={} quotes={}.",
                        leg_ids.size(),
                        time_grid.NumSteps(),
                        buffer.NumPaths(),
                        market_config.quote_remarks);

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

        std::vector<LegExposureIdentity> leg_identities;
        leg_identities.reserve(legs.size());
        for (const PathPricingLegEntry& leg : legs) {
            leg_identities.push_back(LegExposureIdentity{.leg_id = leg.leg_id, .trade_id = leg.trade_id});
        }

        const bool exposure_dumped =
                DumpLegExposurePathsIfEnvSet(leg_pv, time_grid, leg_identities);
        if (exposure_dumped) {
            Logger::NumInfo(
                    "simulate: leg exposure CSV written (NUMERAIRE_DUMP_LEG_EXPOSURE, all {} paths).",
                    buffer.NumPaths());
        }

        if (persist_exposure) {
            std::vector<LegExposureMetrics> metrics;
            ComputeLegExposureMetrics(leg_pv, leg_identities, time_grid, metrics);

            database::SqliteTradeLegExposureRepository exposure_repo(db_path.string());
            const std::string batch_run_id = MakeExposureBatchRunId();
            for (const LegExposureMetrics& metric : metrics) {
                database::TradeLegExposureEodRow row{};
                row.as_of = as_of;
                row.trade_id = metric.trade_id;
                row.leg_id = metric.leg_id;
                row.pillar_id = metric.pillar_id;
                row.grid_step = metric.grid_step;
                row.year_fraction = metric.year_fraction;
                row.exposure_date = metric.exposure_date;
                row.ee = metric.ee;
                row.pfe_95 = metric.pfe_95;
                row.pfe_975 = metric.pfe_975;
                row.num_paths = num_paths;
                row.mc_seed = seed;
                row.calibration_id = calibration_id;
                row.scope_key = book;
                row.batch_run_id = batch_run_id;
                row.pricing_engine = kPathExposurePricingEngine;
                row.remarks = market_config.quote_remarks;
                exposure_repo.Upsert(row);
            }

            Logger::NumInfo(
                    "simulate: persisted {} exposure row(s) to trade_leg_exposure_eod batch_run_id={}.",
                    metrics.size(),
                    batch_run_id);
        }
    }

    return 0;
}

}  // namespace numeraire::simulation
