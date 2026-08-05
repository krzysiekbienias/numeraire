#include <numeraire/pricers/monte_carlo_gbm_european_pricer.hpp>

#include <numeraire/core/imarket_data.hpp>
#include <numeraire/core/iproduct.hpp>
#include <numeraire/core/pricing_result.hpp>
#include <numeraire/enums/exercise_style.hpp>
#include <numeraire/enums/option_type.hpp>
#include <numeraire/products/vanilla_equity_option_product.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/simulation/exposure_time_grid.hpp>
#include <numeraire/simulation/gbm_evolution.hpp>
#include <numeraire/simulation/gbm_spec.hpp>
#include <numeraire/simulation/random_engine.hpp>
#include <numeraire/simulation/scenario_buffer.hpp>
#include <numeraire/utils/config.hpp>
#include <numeraire/utils/exception.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <span>
#include <string>

namespace numeraire::pricers {
namespace {

// Deliberately distinct from NUMERAIRE_MC_* and `pricing.monte_carlo`, which drive the
// exposure simulation: sampling one terminal value per path is a different workload
// from walking the exposure grid, and the two must be tunable apart.
constexpr const char* kPathsEnv = "NUMERAIRE_MC_PRICER_PATHS";
constexpr const char* kSeedEnv = "NUMERAIRE_MC_PRICER_SEED";
constexpr const char* kDefaultConfigPath = "configs/default.json";
constexpr const char* kPathsConfigKey = "pricing.monte_carlo_pricer.default_paths";
constexpr const char* kSeedConfigKey = "pricing.monte_carlo_pricer.default_seed";

/// Two paths is the minimum for an unbiased sample variance, hence a standard error.
[[nodiscard]] std::size_t ClampPaths(const std::size_t num_paths) {
    return std::max<std::size_t>(2, num_paths);
}

[[nodiscard]] std::optional<long long> PositiveLongFromEnv(const char* name) {
    const char* raw = std::getenv(name);
    if (raw == nullptr || raw[0] == '\0') {
        return std::nullopt;
    }
    try {
        const long long parsed = std::stoll(raw);
        if (parsed <= 0) {
            return std::nullopt;
        }
        return parsed;
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<long long> PositiveLongFromCommittedConfig(const char* key) {
    try {
        const utils::Config cfg = utils::Config::Load(kDefaultConfigPath);
        const long long value = cfg.RequireAt(key).get<long long>();
        if (value <= 0) {
            return std::nullopt;
        }
        return value;
    } catch (const ConfigError&) {
        return std::nullopt;
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

[[nodiscard]] std::size_t ResolveDefaultPaths() {
    if (const auto from_env = PositiveLongFromEnv(kPathsEnv); from_env.has_value()) {
        return ClampPaths(static_cast<std::size_t>(*from_env));
    }
    if (const auto from_cfg = PositiveLongFromCommittedConfig(kPathsConfigKey); from_cfg.has_value()) {
        return ClampPaths(static_cast<std::size_t>(*from_cfg));
    }
    return MonteCarloGbmEuropeanPricer::kFallbackPaths;
}

[[nodiscard]] std::uint64_t ResolveDefaultSeed() {
    if (const auto from_env = PositiveLongFromEnv(kSeedEnv); from_env.has_value()) {
        return static_cast<std::uint64_t>(*from_env);
    }
    if (const auto from_cfg = PositiveLongFromCommittedConfig(kSeedConfigKey); from_cfg.has_value()) {
        return static_cast<std::uint64_t>(*from_cfg);
    }
    return MonteCarloGbmEuropeanPricer::kFallbackSeed;
}

[[nodiscard]] double Payoff(const OptionType kind, const double spot, const double strike) {
    return kind == OptionType::kCall ? std::max(spot - strike, 0.0) : std::max(strike - spot, 0.0);
}

[[nodiscard]] std::string FormatDouble(const double value) {
    std::array<char, 32> buf{};
    std::snprintf(buf.data(), buf.size(), "%.6g", value);
    return std::string(buf.data());
}

/// Valuation date and expiry only — one exact GBM step straight to maturity.
[[nodiscard]] simulation::ExposureTimeGrid TerminalGrid(const schedule::Date& valuation_date,
                                                        const schedule::Date& expiry_date,
                                                        const double tau) {
    simulation::ExposureTimeGrid grid;
    grid.valuation_date = valuation_date;
    grid.nodes.resize(2);
    grid.nodes[0].date = valuation_date;
    grid.nodes[0].year_fraction = 0.0;
    grid.nodes[0].pillar_id = "spot";
    grid.nodes[1].date = expiry_date;
    grid.nodes[1].year_fraction = tau;
    grid.nodes[1].pillar_id = "expiry";
    return grid;
}

[[nodiscard]] core::PricingResult PriceEuropean(const products::VanillaEquityOptionProduct& vanilla,
                                                const core::IMarketData& market,
                                                const std::size_t num_paths,
                                                const std::uint64_t seed) {
    if (vanilla.Exercise() != ExerciseStyle::kEuropean) {
        throw ValidationError("MonteCarloGbmEuropeanPricer supports European exercise only");
    }

    const double tau = schedule::Act365FixedYearFraction(market.ValuationDate(), vanilla.ExpiryDate());
    const double spot = market.Spot(vanilla.UnderlyingId());
    const double strike = vanilla.Strike();
    const double r = market.RiskFreeRateForTenor(tau);
    const double q = market.DividendYield(vanilla.UnderlyingId());
    const OptionType kind = vanilla.OptionKind();

    core::PricingResult result;
    core::PricingMetadata meta;

    if (tau <= 0.0) {
        // Expired or same-day: the payoff is known, so sampling it would add noise
        // for nothing.
        result.SetNpv(Payoff(kind, spot, strike));
        meta.diagnostics = "mc_paths=0; mc_intrinsic=1";
        result.SetMetadata(std::move(meta));
        return result;
    }

    const double vol = market.ImpliedVolatility(vanilla.UnderlyingId(), strike, tau, kind);

    simulation::ScenarioBuffer buffer(1U, 2U, num_paths);
    simulation::MersenneTwisterEngine engine(seed);
    const simulation::SingleFactorGbmSpec spec{spot, r, q, vol};
    simulation::EvolveSingleFactorGbm(
            buffer, TerminalGrid(market.ValuationDate(), vanilla.ExpiryDate(), tau), spec, engine);

    double sum = 0.0;
    double sum_squares = 0.0;
    const std::span<const double> terminal = buffer.Slab(0U, 1U);
    for (const double spot_at_expiry : terminal) {
        const double payoff = Payoff(kind, spot_at_expiry, strike);
        sum += payoff;
        sum_squares += payoff * payoff;
    }

    const double n = static_cast<double>(num_paths);
    const double mean = sum / n;
    // Unbiased sample variance of the payoff; the mean of n draws is sqrt(n) tighter.
    const double variance = std::max((sum_squares - (n * mean * mean)) / (n - 1.0), 0.0);
    const double discount = std::exp(-r * tau);
    const double npv = discount * mean;
    const double std_error = discount * std::sqrt(variance / n);

    result.SetNpv(npv);

    std::string diagnostics = "mc_paths=" + std::to_string(num_paths) + "; mc_seed=" + std::to_string(seed) +
                              "; mc_std_err=" + FormatDouble(std_error);
    if (npv > 0.0) {
        diagnostics += "; mc_std_err_pct=" + FormatDouble(100.0 * std_error / npv);
    }
    meta.diagnostics = std::move(diagnostics);
    result.SetMetadata(std::move(meta));
    return result;
}

}  // namespace

MonteCarloGbmEuropeanPricer::MonteCarloGbmEuropeanPricer()
    : num_paths_(ResolveDefaultPaths()), seed_(ResolveDefaultSeed()) {}

MonteCarloGbmEuropeanPricer::MonteCarloGbmEuropeanPricer(const std::size_t num_paths, const std::uint64_t seed)
    : num_paths_(ClampPaths(num_paths)), seed_(seed) {}

numeraire::PricingEngineType MonteCarloGbmEuropeanPricer::EngineKind() const {
    return numeraire::PricingEngineType::kMonteCarlo;
}

core::PricingResult MonteCarloGbmEuropeanPricer::Price(const core::IProduct& product,
                                                       const core::IMarketData& market) const {
    if (const auto* vanilla = dynamic_cast<const products::VanillaEquityOptionProduct*>(&product)) {
        return PriceEuropean(*vanilla, market, num_paths_, seed_);
    }
    throw ValidationError("MonteCarloGbmEuropeanPricer requires VanillaEquityOptionProduct");
}

}  // namespace numeraire::pricers
