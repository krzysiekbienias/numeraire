#include <numeraire/pricers/binomial_black_scholes_equity_pricer.hpp>

#include <numeraire/core/imarket_data.hpp>
#include <numeraire/core/iproduct.hpp>
#include <numeraire/core/pricing_result.hpp>
#include <numeraire/enums/exercise_style.hpp>
#include <numeraire/enums/option_type.hpp>
#include <numeraire/products/vanilla_equity_option_product.hpp>
#include <numeraire/quant/cox_ross_rubinstein.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/utils/config.hpp>
#include <numeraire/utils/exception.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>

namespace numeraire::pricers {
namespace {

constexpr double kSpotRelBump = 1.0e-4;
constexpr double kVolAbsBump = 1.0e-4;
constexpr double kRateAbsBump = 1.0e-5;
constexpr double kDay = 1.0 / 365.0;
constexpr const char* kBinomialStepsEnv = "NUMERAIRE_BINOMIAL_STEPS";
constexpr const char* kDefaultConfigPath = "configs/default.json";
constexpr const char* kBinomialStepsConfigKey = "pricing.binomial.default_steps";

[[nodiscard]] std::size_t ClampSteps(const std::size_t n_steps) {
    return std::max<std::size_t>(1, n_steps);
}

[[nodiscard]] std::optional<std::size_t> StepsFromEnv() {
    const char* raw = std::getenv(kBinomialStepsEnv);
    if (raw == nullptr || raw[0] == '\0') {
        return std::nullopt;
    }
    try {
        const long parsed = std::stol(raw);
        if (parsed <= 0) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(parsed);
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<std::size_t> StepsFromCommittedConfig() {
    try {
        const utils::Config cfg = utils::Config::Load(kDefaultConfigPath);
        const int steps = cfg.RequireAt(kBinomialStepsConfigKey).get<int>();
        if (steps <= 0) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(steps);
    } catch (const ConfigError&) {
        return std::nullopt;
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

[[nodiscard]] std::size_t ResolveDefaultBinomialSteps() {
    if (const auto from_env = StepsFromEnv(); from_env.has_value()) {
        return ClampSteps(*from_env);
    }
    if (const auto from_cfg = StepsFromCommittedConfig(); from_cfg.has_value()) {
        return ClampSteps(*from_cfg);
    }
    return BinomialBlackScholesEquityPricer::kFallbackSteps;
}

[[nodiscard]] double TreeNpv(const OptionType kind,
                             const ExerciseStyle exercise,
                             const double spot,
                             const double strike,
                             const double r,
                             const double q,
                             const double vol,
                             const double tau,
                             const std::size_t n_steps) {
    return quant::CoxRossRubinsteinVanillaPrice(kind, exercise, spot, strike, r, q, vol, tau, n_steps);
}

[[nodiscard]] core::PricingGreeks BumpGreeks(const OptionType kind,
                                             const ExerciseStyle exercise,
                                             const double spot,
                                             const double strike,
                                             const double r,
                                             const double q,
                                             const double vol,
                                             const double tau,
                                             const double npv0,
                                             const std::size_t n_steps) {
    core::PricingGreeks g;

    const double h = std::max(spot * kSpotRelBump, 1.0e-8);
    const double up = TreeNpv(kind, exercise, spot + h, strike, r, q, vol, tau, n_steps);
    const double dn = TreeNpv(kind, exercise, spot - h, strike, r, q, vol, tau, n_steps);
    g.delta = (up - dn) / (2.0 * h);
    g.gamma = (up - 2.0 * npv0 + dn) / (h * h);

    if (vol > 0.0) {
        const double dv = kVolAbsBump;
        const double v_up = TreeNpv(kind, exercise, spot, strike, r, q, vol + dv, tau, n_steps);
        const double v_dn = TreeNpv(kind, exercise, spot, strike, r, q, std::max(vol - dv, 0.0), tau, n_steps);
        g.vega = (v_up - v_dn) / (2.0 * dv);
    } else {
        g.vega = 0.0;
    }

    {
        const double dr = kRateAbsBump;
        const double r_up = TreeNpv(kind, exercise, spot, strike, r + dr, q, vol, tau, n_steps);
        const double r_dn = TreeNpv(kind, exercise, spot, strike, r - dr, q, vol, tau, n_steps);
        g.rho = (r_up - r_dn) / (2.0 * dr);
    }

    if (tau > kDay) {
        const double v_short = TreeNpv(kind, exercise, spot, strike, r, q, vol, tau - kDay, n_steps);
        // Calendar theta as annualized rate: ∂V/∂t ≈ (V(t+1d) - V(t)) / (1/365).
        g.theta = (v_short - npv0) / kDay;
    } else {
        g.theta = 0.0;
    }

    return g;
}

[[nodiscard]] core::PricingResult PriceVanilla(const products::VanillaEquityOptionProduct& vanilla,
                                               const core::IMarketData& market,
                                               const std::size_t n_steps) {
    const ExerciseStyle exercise = vanilla.Exercise();
    if (exercise != ExerciseStyle::kEuropean && exercise != ExerciseStyle::kAmerican) {
        throw ValidationError("BinomialBlackScholesEquityPricer supports European or American exercise only");
    }

    const double tau = schedule::Act365FixedYearFraction(market.ValuationDate(), vanilla.ExpiryDate());
    const double spot = market.Spot(vanilla.UnderlyingId());
    const double strike = vanilla.Strike();
    const double r = market.RiskFreeRateForTenor(tau);
    const double q = market.DividendYield(vanilla.UnderlyingId());
    const OptionType kind = vanilla.OptionKind();

    core::PricingResult result;

    if (tau <= 0.0) {
        result.SetNpv(quant::CoxRossRubinsteinVanillaPrice(
                kind, exercise, spot, strike, r, q, 0.0, 0.0, n_steps));
        return result;
    }

    const double vol = market.ImpliedVolatility(vanilla.UnderlyingId(), strike, tau, kind);
    const double npv = TreeNpv(kind, exercise, spot, strike, r, q, vol, tau, n_steps);
    result.SetNpv(npv);
    result.SetGreeks(BumpGreeks(kind, exercise, spot, strike, r, q, vol, tau, npv, n_steps));

    core::PricingMetadata meta;
    meta.diagnostics = "crr_n_steps=" + std::to_string(n_steps);
    result.SetMetadata(std::move(meta));
    return result;
}

}  // namespace

BinomialBlackScholesEquityPricer::BinomialBlackScholesEquityPricer()
    : n_steps_(ResolveDefaultBinomialSteps()) {}

BinomialBlackScholesEquityPricer::BinomialBlackScholesEquityPricer(const std::size_t n_steps)
    : n_steps_(ClampSteps(n_steps)) {}

numeraire::PricingEngineType BinomialBlackScholesEquityPricer::EngineKind() const {
    return numeraire::PricingEngineType::kBinomialTree;
}

core::PricingResult BinomialBlackScholesEquityPricer::Price(const core::IProduct& product,
                                                            const core::IMarketData& market) const {
    if (const auto* vanilla = dynamic_cast<const products::VanillaEquityOptionProduct*>(&product)) {
        return PriceVanilla(*vanilla, market, n_steps_);
    }
    throw ValidationError("BinomialBlackScholesEquityPricer requires VanillaEquityOptionProduct");
}

}  // namespace numeraire::pricers
