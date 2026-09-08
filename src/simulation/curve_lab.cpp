#include <numeraire/simulation/curve_lab.hpp>

#include <numeraire/database/futures_pillar_returns.hpp>
#include <numeraire/database/sqlite_schema.hpp>
#include <numeraire/quant/cholesky.hpp>
#include <numeraire/quant/gabillon_curve_fit.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/simulation/exposure_grid_config.hpp>
#include <numeraire/simulation/gabillon_evolution.hpp>
#include <numeraire/simulation/random_engine.hpp>
#include <numeraire/simulation/scenario_dump.hpp>
#include <numeraire/utils/database_path.hpp>
#include <numeraire/utils/exception.hpp>
#include <numeraire/utils/logger.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace numeraire::simulation {
namespace {

using numeraire::utils::Logger;
using numeraire::utils::ResolveDatabasePath;

[[nodiscard]] bool LooksIsoDate(const std::string& s) {
    return s.size() == 10U && s[4] == '-' && s[7] == '-';
}

[[nodiscard]] std::filesystem::path ResolveExposureGridConfigPath() {
    if (const std::optional<std::filesystem::path> from_defaults =
                ExposureGridConfigPathFromDefaults("configs/default.json")) {
        return *from_defaults;
    }
    return std::filesystem::path{"configs/simulation_exposure_grid.json"};
}

/// Quantile by linear interpolation on an already sorted sample.
[[nodiscard]] double Quantile(const std::vector<double>& sorted, const double q) {
    if (sorted.empty()) {
        return 0.0;
    }
    const double position = q * static_cast<double>(sorted.size() - 1U);
    const auto lower = static_cast<std::size_t>(std::floor(position));
    const std::size_t upper = std::min(lower + 1U, sorted.size() - 1U);
    const double weight = position - static_cast<double>(lower);
    return ((1.0 - weight) * sorted[lower]) + (weight * sorted[upper]);
}

/// Variance the evolution kernel integrates for one contract, reproduced step by step.
///
/// Deliberately mirrors the kernel's midpoint scheme rather than integrating the closed
/// form, so that comparing it against the realized spread tests the discretisation as it
/// actually runs instead of the algebra it was derived from.
[[nodiscard]] double IntegratedModelVariance(const GabillonContract& contract,
                                             const GabillonCurveParams& curve,
                                             const double factor_correlation,
                                             const ExposureTimeGrid& time_grid) {
    double total = 0.0;
    for (std::size_t step = 1; step < time_grid.NumSteps(); ++step) {
        const double from_years = time_grid.nodes[step - 1].year_fraction;
        if (contract.settlement_years <= from_years) {
            break;
        }
        const double dt = std::min(time_grid.nodes[step].year_fraction, contract.settlement_years) - from_years;
        const double tenor = contract.settlement_years - (from_years + (0.5 * dt));
        const double vol = quant::GabillonForwardVolatility(tenor, curve.mean_reversion,
                                                            curve.short_factor_vol, curve.long_factor_vol,
                                                            factor_correlation);
        total += vol * vol * dt;
    }
    return total;
}

[[nodiscard]] int EnvInt(const char* key, const int default_value) {
    const char* raw = std::getenv(key);
    if (raw == nullptr || raw[0] == '\0') {
        return default_value;
    }
    char* end = nullptr;
    const std::int64_t value = std::strtoll(raw, &end, 10);
    return end == raw ? default_value : static_cast<int>(value);
}

}  // namespace

GabillonSimulationSpec BuildSingleCurveSpec(const std::string& database_file_path,
                                            const std::string_view product_code, const std::string_view as_of,
                                            const GabillonCurveDynamics& dynamics,
                                            const std::size_t max_contracts) {
    if (max_contracts == 0U) {
        throw ValidationError("BuildSingleCurveSpec: max_contracts must be > 0.");
    }
    const std::vector<database::FuturesCurveQuote> quotes =
            database::LoadFuturesCurveSnapshot(database_file_path, product_code, as_of);
    if (quotes.empty()) {
        throw ValidationError("BuildSingleCurveSpec: no quoted " + std::string(product_code) +
                              " contracts on " + std::string(as_of) + ".");
    }

    const std::array<double, 4> correlation_matrix{1.0, dynamics.factor_correlation,
                                                   dynamics.factor_correlation, 1.0};
    const auto correlation = quant::CholeskyDecompose(correlation_matrix, 2U);
    if (correlation.status != quant::CholeskyStatus::kOk) {
        throw ValidationError("BuildSingleCurveSpec: factor correlation " +
                              std::to_string(dynamics.factor_correlation) +
                              " does not admit a Cholesky factor.");
    }

    GabillonSimulationSpec spec;
    spec.curves.push_back(dynamics.params);
    spec.cholesky = correlation.factor;

    const schedule::Date valuation_date = schedule::ParseIsoDate(std::string(as_of));
    for (const database::FuturesCurveQuote& quote : quotes) {
        if (spec.contracts.size() >= max_contracts) {
            break;
        }
        if (!(quote.price > 0.0)) {
            continue;
        }
        spec.contracts.push_back(GabillonContract{
                .contract_ticker = quote.ticker,
                .curve_index = 0U,
                .settlement_years = schedule::Act365FixedYearFraction(
                        valuation_date, schedule::ParseIsoDate(quote.settlement_date)),
                .anchor_price = quote.price,
        });
    }
    if (spec.contracts.empty()) {
        throw ValidationError("BuildSingleCurveSpec: every quoted " + std::string(product_code) +
                              " contract on " + std::string(as_of) + " had a non-positive settle.");
    }
    return spec;
}

CurveLabResult SummarizeCurveLab(const ScenarioBuffer& buffer, const ExposureTimeGrid& time_grid,
                                 const GabillonSimulationSpec& spec) {
    if (buffer.NumFactors() != spec.contracts.size()) {
        throw ValidationError("SummarizeCurveLab: buffer must hold one factor per contract.");
    }
    if (buffer.NumSteps() != time_grid.NumSteps() || time_grid.NumSteps() == 0U) {
        throw ValidationError("SummarizeCurveLab: buffer steps must match a non-empty time_grid.");
    }

    const std::size_t terminal_step = time_grid.NumSteps() - 1U;
    const double horizon = time_grid.nodes[terminal_step].year_fraction;
    const auto num_paths = static_cast<double>(buffer.NumPaths());

    CurveLabResult result;
    result.contracts.reserve(spec.contracts.size());

    for (std::size_t index = 0; index < spec.contracts.size(); ++index) {
        const GabillonContract& contract = spec.contracts[index];
        const GabillonCurveParams& curve = spec.curves[contract.curve_index];

        std::vector<double> terminal(buffer.NumPaths());
        double sum = 0.0;
        for (std::size_t path = 0; path < buffer.NumPaths(); ++path) {
            terminal[path] = buffer.At(index, terminal_step, path);
            sum += terminal[path];
        }
        std::sort(terminal.begin(), terminal.end());

        double log_mean = 0.0;
        for (const double value : terminal) {
            log_mean += std::log(value / contract.anchor_price);
        }
        log_mean /= num_paths;
        double log_variance = 0.0;
        for (const double value : terminal) {
            const double d = std::log(value / contract.anchor_price) - log_mean;
            log_variance += d * d;
        }
        log_variance /= num_paths - 1.0;

        const double diffusion_years = std::min(horizon, std::max(contract.settlement_years, 0.0));
        const double factor_correlation = ShockCorrelationFromCholesky(spec.cholesky, contract.curve_index * 2U,
                                                                       (contract.curve_index * 2U) + 1U);
        const double model_variance = IntegratedModelVariance(contract, curve, factor_correlation, time_grid);

        CurveLabContractStat stat;
        stat.contract_ticker = contract.contract_ticker;
        stat.settlement_years = contract.settlement_years;
        stat.anchor_price = contract.anchor_price;
        stat.diffusion_years = diffusion_years;
        stat.mean_terminal = sum / num_paths;
        stat.p5 = Quantile(terminal, 0.05);
        stat.p50 = Quantile(terminal, 0.50);
        stat.p95 = Quantile(terminal, 0.95);
        stat.realized_vol = diffusion_years > 0.0 ? std::sqrt(log_variance / diffusion_years) : 0.0;
        stat.model_vol = diffusion_years > 0.0 ? std::sqrt(model_variance / diffusion_years) : 0.0;

        result.worst_martingale_drift = std::max(
                result.worst_martingale_drift, std::abs((stat.mean_terminal / contract.anchor_price) - 1.0));
        result.contracts.push_back(std::move(stat));
    }
    return result;
}

void PrintCurveLabUsageLines() {
    Logger::NumError(
            "  dev_main --curve-lab --product CODE --as-of YYYY-MM-DD "
            "[--book PORTFOLIO_ID] [--mean-reversion K] [--short-vol S] [--long-vol L] [--rho R] "
            "[--max-contracts N] [--paths N] [--seed N] [--csv PATH]\n"
            "    Evolve one commodity curve on its own, away from any book, through the same Gabillon "
            "kernel production exposure runs use.\n"
            "    Contracts are the whole quoted strip on `--as-of` (capped by `--max-contracts`), each "
            "anchored on its own settle, so the curve keeps today's shape and only its volatility decays "
            "with maturity.\n"
            "    `--book` seeds the parameters from that book's `gabillon_2f`/`fit` snapshot; the four "
            "parameter flags override whatever was loaded, and without `--book` all of "
            "`--mean-reversion`, `--short-vol` and `--long-vol` are required (`--rho` defaults to 0).\n"
            "    Reports per contract: anchor, terminal quantiles, and realized against model volatility — "
            "the two agreeing is the check that the paths carry the dynamics the parameters describe.\n"
            "    Env: NUMERAIRE_MC_PATHS, NUMERAIRE_MC_SEED, NUMERAIRE_DUMP_SCENARIOS.");
}

int TryRunCurveLab(const int argc, char** argv, const numeraire::utils::Config& cfg) {
    bool mode = false;
    std::string product;
    std::string as_of;
    std::string book;
    std::string csv_path;
    std::optional<double> mean_reversion;
    std::optional<double> short_vol;
    std::optional<double> long_vol;
    std::optional<double> rho;
    int max_contracts = 24;
    int num_paths = EnvInt("NUMERAIRE_MC_PATHS", 20000);
    int seed = EnvInt("NUMERAIRE_MC_SEED", 42);

    const auto require_value = [argc, argv](int& i, const char* flag) -> const char* {
        if (i + 1 >= argc) {
            Logger::NumError("{} requires a value.", flag);
            return nullptr;
        }
        return argv[++i];
    };

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "--curve-lab") == 0) {
            mode = true;
            continue;
        }
        const char* value = nullptr;
        if (std::strcmp(arg, "--product") == 0) {
            if ((value = require_value(i, arg)) == nullptr) {
                return 1;
            }
            product = value;
        } else if (std::strcmp(arg, "--as-of") == 0) {
            if ((value = require_value(i, arg)) == nullptr) {
                return 1;
            }
            as_of = value;
        } else if (std::strcmp(arg, "--book") == 0) {
            if ((value = require_value(i, arg)) == nullptr) {
                return 1;
            }
            book = value;
        } else if (std::strcmp(arg, "--csv") == 0) {
            if ((value = require_value(i, arg)) == nullptr) {
                return 1;
            }
            csv_path = value;
        } else if (std::strcmp(arg, "--mean-reversion") == 0) {
            if ((value = require_value(i, arg)) == nullptr) {
                return 1;
            }
            mean_reversion = std::atof(value);
        } else if (std::strcmp(arg, "--short-vol") == 0) {
            if ((value = require_value(i, arg)) == nullptr) {
                return 1;
            }
            short_vol = std::atof(value);
        } else if (std::strcmp(arg, "--long-vol") == 0) {
            if ((value = require_value(i, arg)) == nullptr) {
                return 1;
            }
            long_vol = std::atof(value);
        } else if (std::strcmp(arg, "--rho") == 0) {
            if ((value = require_value(i, arg)) == nullptr) {
                return 1;
            }
            rho = std::atof(value);
        } else if (std::strcmp(arg, "--max-contracts") == 0) {
            if ((value = require_value(i, arg)) == nullptr) {
                return 1;
            }
            max_contracts = std::atoi(value);
        } else if (std::strcmp(arg, "--paths") == 0) {
            if ((value = require_value(i, arg)) == nullptr) {
                return 1;
            }
            num_paths = std::atoi(value);
        } else if (std::strcmp(arg, "--seed") == 0) {
            if ((value = require_value(i, arg)) == nullptr) {
                return 1;
            }
            seed = std::atoi(value);
        }
    }

    if (!mode) {
        return -1;
    }
    if (product.empty() || as_of.empty()) {
        Logger::NumError("--curve-lab requires --product CODE and --as-of YYYY-MM-DD.");
        PrintCurveLabUsageLines();
        return 1;
    }
    if (!LooksIsoDate(as_of)) {
        Logger::NumError("--as-of must be YYYY-MM-DD.");
        return 1;
    }
    if (num_paths <= 1 || max_contracts <= 0) {
        Logger::NumError("--paths must be > 1 and --max-contracts > 0.");
        return 1;
    }

    const std::filesystem::path db_path = ResolveDatabasePath(cfg);
    database::BootstrapTradeDatabaseSchema(db_path, "sql/schema_v1.sql");

    GabillonCurveDynamics dynamics;
    dynamics.params.product_code = product;
    std::string parameter_source = "hand-set";
    if (!book.empty()) {
        const std::optional<GabillonCurveDynamics> calibrated =
                TryLoadGabillonCurveDynamics(db_path.string(), book, as_of, product);
        if (!calibrated.has_value()) {
            Logger::NumError(
                    "curve-lab: no gabillon_2f/fit snapshot carrying {} for scope_key={} with as_of <= {}.",
                    product, book, as_of);
            return 1;
        }
        dynamics = *calibrated;
        parameter_source = "calibrated(" + book + ")";
    }
    if (mean_reversion.has_value()) {
        dynamics.params.mean_reversion = *mean_reversion;
    }
    if (short_vol.has_value()) {
        dynamics.params.short_factor_vol = *short_vol;
    }
    if (long_vol.has_value()) {
        dynamics.params.long_factor_vol = *long_vol;
    }
    if (rho.has_value()) {
        dynamics.factor_correlation = *rho;
    }
    if (!book.empty() && (mean_reversion || short_vol || long_vol || rho)) {
        parameter_source += "+overrides";
    }
    if (book.empty() && !(mean_reversion.has_value() && short_vol.has_value() && long_vol.has_value())) {
        Logger::NumError(
                "curve-lab: without --book you must supply --mean-reversion, --short-vol and --long-vol.");
        PrintCurveLabUsageLines();
        return 1;
    }

    try {
        const GabillonSimulationSpec spec = BuildSingleCurveSpec(
                db_path.string(), product, as_of, dynamics, static_cast<std::size_t>(max_contracts));

        const ExposureGridConfig grid_cfg = LoadExposureGridConfig(ResolveExposureGridConfigPath());
        const ExposureTimeGrid time_grid =
                BuildExposureTimeGrid(grid_cfg, schedule::ParseIsoDate(as_of), std::nullopt);

        ScenarioBuffer buffer(spec.NumFactors(), time_grid.NumSteps(), static_cast<std::size_t>(num_paths));
        MersenneTwisterEngine engine(static_cast<std::uint64_t>(seed));
        EvolveGabillonCurves(buffer, time_grid, spec, engine);

        const CurveLabResult result = SummarizeCurveLab(buffer, time_grid, spec);

        Logger::NumInfo(
                "curve-lab {}: as_of={} params={} k={:.3f}/yr sigma_short={:.1f}% sigma_long={:.1f}% "
                "rho={:+.3f}; contracts={} paths={} seed={} grid_steps={} horizon={:.2f}y.",
                product, as_of, parameter_source, dynamics.params.mean_reversion,
                100.0 * dynamics.params.short_factor_vol, 100.0 * dynamics.params.long_factor_vol,
                dynamics.factor_correlation, spec.contracts.size(), num_paths, seed, time_grid.NumSteps(),
                time_grid.nodes[time_grid.NumSteps() - 1U].year_fraction);
        Logger::NumInfo("  {:<10} {:>7} {:>10} {:>10} {:>10} {:>10} {:>9} {:>9}", "contract", "tenor",
                        "anchor", "p5", "median", "p95", "vol_real", "vol_model");
        for (const CurveLabContractStat& stat : result.contracts) {
            Logger::NumInfo("  {:<10} {:>7.3f} {:>10.4f} {:>10.4f} {:>10.4f} {:>10.4f} {:>8.1f}% {:>8.1f}%",
                            stat.contract_ticker, stat.settlement_years, stat.anchor_price, stat.p5, stat.p50,
                            stat.p95, 100.0 * stat.realized_vol, 100.0 * stat.model_vol);
        }
        Logger::NumInfo(
                "curve-lab finished: worst martingale drift {:.3f}% (futures carry no drift, so this is "
                "discretisation plus sampling error).",
                100.0 * result.worst_martingale_drift);

        std::vector<std::string> tickers;
        tickers.reserve(spec.contracts.size());
        for (const GabillonContract& contract : spec.contracts) {
            tickers.push_back(contract.contract_ticker);
        }
        if (!csv_path.empty()) {
            DumpMultiFactorScenarioPathsCsv(std::filesystem::path{csv_path}, buffer, time_grid,
                                            std::span<const std::string>(tickers));
            Logger::NumInfo("curve-lab: wrote {} path(s) per contract to {}.", buffer.NumPaths(), csv_path);
        } else if (DumpMultiFactorScenarioPathsIfEnvSet(buffer, time_grid,
                                                        std::span<const std::string>(tickers))) {
            Logger::NumInfo("curve-lab: scenario CSV written (NUMERAIRE_DUMP_SCENARIOS).");
        }
    } catch (const ValidationError& ex) {
        Logger::NumError("curve-lab failed: {}.", ex.what());
        return 1;
    }

    return 0;
}

}  // namespace numeraire::simulation
