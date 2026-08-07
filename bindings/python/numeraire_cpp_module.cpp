#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numeraire/core/imarket_data.hpp>
#include <numeraire/core/ipricer.hpp>
#include <numeraire/core/pricing_result.hpp>
#include <numeraire/enums/exercise_style.hpp>
#include <numeraire/enums/model_type.hpp>
#include <numeraire/enums/option_type.hpp>
#include <numeraire/enums/pricing_engine_type.hpp>
#include <numeraire/pricers/binomial_black_scholes_equity_pricer.hpp>
#include <numeraire/pricers/monte_carlo_gbm_european_pricer.hpp>
#include <numeraire/pricers/pricer_factory.hpp>
#include <numeraire/products/equity_asset_or_nothing_product.hpp>
#include <numeraire/products/equity_cash_or_nothing_product.hpp>
#include <numeraire/products/equity_forward_product.hpp>
#include <numeraire/products/vanilla_equity_option_product.hpp>
#include <numeraire/quant/cox_ross_rubinstein.hpp>
#include <numeraire/quant/interest_rate_transforms.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/simulation/exposure_time_grid.hpp>
#include <numeraire/simulation/gbm_evolution.hpp>
#include <numeraire/simulation/gbm_spec.hpp>
#include <numeraire/simulation/random_engine.hpp>
#include <numeraire/simulation/scenario_buffer.hpp>
#include <numeraire/utils/exception.hpp>
#include <stdexcept>
#include <string>

namespace py = pybind11;

namespace {

class FlatMarket final : public numeraire::core::IMarketData {
public:
    FlatMarket(numeraire::schedule::Date valuation, double spot, double rate, double div, double vol)
        : valuation_(valuation), spot_(spot), rate_(rate), div_(div), vol_(vol) {}

    [[nodiscard]] const numeraire::schedule::Date& ValuationDate() const override { return valuation_; }

    [[nodiscard]] double Spot(const std::string_view) const override { return spot_; }

    [[nodiscard]] double RiskFreeRate() const override { return rate_; }

    [[nodiscard]] double DividendYield(const std::string_view) const override { return div_; }

    [[nodiscard]] double ImpliedVolatility(const std::string_view,
                                           const double,
                                           const double,
                                           const numeraire::OptionType) const override {
        return vol_;
    }

private:
    numeraire::schedule::Date valuation_;
    double spot_;
    double rate_;
    double div_;
    double vol_;
};

struct LabDates {
    numeraire::schedule::Date valuation;
    numeraire::schedule::Date expiry;
};

[[nodiscard]] LabDates MakeLabDates(const double tau_years) {
    if (tau_years < 0.0) {
        throw py::value_error("tau_years must be non-negative");
    }
    const numeraire::schedule::Date valuation{.year = 2025, .month = 1, .day = 1};
    const int days = static_cast<int>(std::llround(std::max(0.0, tau_years) * 365.0));
    return LabDates{valuation, numeraire::schedule::AddCalendarDays(valuation, days)};
}

[[nodiscard]] py::dict ResultToDict(const numeraire::core::PricingResult& result,
                                    const std::string& engine_label,
                                    const LabDates& dates) {
    if (!result.Npv().has_value()) {
        throw std::runtime_error("pricer returned empty NPV");
    }
    py::dict out;
    out["ok"] = true;
    out["engine"] = engine_label;
    out["npv"] = *result.Npv();
    out["tau_years_used"] = numeraire::schedule::Act365FixedYearFraction(dates.valuation, dates.expiry);

    if (result.Greeks().has_value()) {
        const auto& g = *result.Greeks();
        if (g.delta) {
            out["delta"] = *g.delta;
        }
        if (g.gamma) {
            out["gamma"] = *g.gamma;
        }
        if (g.vega) {
            out["vega"] = *g.vega;
        }
        if (g.theta) {
            out["theta"] = *g.theta;
        }
        if (g.rho) {
            out["rho"] = *g.rho;
        }
    }
    if (result.Metadata().has_value() && result.Metadata()->diagnostics.has_value()) {
        out["diagnostics"] = *result.Metadata()->diagnostics;
    }
    return out;
}

[[nodiscard]] std::unique_ptr<numeraire::core::IPricer> MakeAnalyticPricer() {
    return numeraire::pricers::PricerFactory::Make(numeraire::PricingEngineType::kAnalytic,
                                                   numeraire::ModelType::kBlackScholes);
}

template <typename Product>
[[nodiscard]] py::dict PriceWithAnalytic(const Product& product,
                                         const FlatMarket& market,
                                         const LabDates& dates,
                                         const std::string& engine_label) {
    try {
        const auto pricer = MakeAnalyticPricer();
        return ResultToDict(pricer->Price(product, market), engine_label, dates);
    } catch (const numeraire::ValidationError& e) {
        throw py::value_error(e.what());
    } catch (const numeraire::NumeraireException& e) {
        throw std::runtime_error(e.what());
    }
}

void RequirePositiveSpotStrike(const double spot, const double strike) {
    if (!(spot > 0.0) || !(strike > 0.0)) {
        throw py::value_error("spot and strike must be positive");
    }
}

void RequireNonNegVol(const double vol) {
    if (vol < 0.0) {
        throw py::value_error("vol must be non-negative");
    }
}

[[nodiscard]] py::dict PriceVanilla(const double spot,
                                    const double strike,
                                    const double vol,
                                    const double rate,
                                    const double div,
                                    const double tau_years,
                                    const bool is_call,
                                    const std::string& exercise,
                                    const std::size_t n_steps) {
    RequirePositiveSpotStrike(spot, strike);
    RequireNonNegVol(vol);

    const std::string ex = [&] {
        std::string s = exercise;
        std::transform(
                s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }();

    numeraire::ExerciseStyle style = numeraire::ExerciseStyle::kEuropean;
    if (ex == "american") {
        style = numeraire::ExerciseStyle::kAmerican;
    } else if (ex == "european" || ex.empty()) {
        style = numeraire::ExerciseStyle::kEuropean;
    } else {
        throw py::value_error("exercise must be 'european' or 'american'");
    }

    const LabDates dates = MakeLabDates(tau_years);
    const numeraire::products::VanillaEquityOptionProduct product(
            "LAB",
            is_call ? numeraire::OptionType::kCall : numeraire::OptionType::kPut,
            style,
            strike,
            dates.valuation,
            dates.expiry);
    const FlatMarket market(dates.valuation, spot, rate, div, vol);

    if (style == numeraire::ExerciseStyle::kAmerican) {
        try {
            const std::size_t steps =
                    n_steps == 0 ? numeraire::pricers::BinomialBlackScholesEquityPricer::kFallbackSteps : n_steps;
            const auto pricer = std::make_unique<numeraire::pricers::BinomialBlackScholesEquityPricer>(steps);
            py::dict out = ResultToDict(pricer->Price(product, market), "c++_binomial_crr", dates);
            out["n_steps"] = static_cast<int>(pricer->NSteps());
            return out;
        } catch (const numeraire::ValidationError& e) {
            throw py::value_error(e.what());
        } catch (const numeraire::NumeraireException& e) {
            throw std::runtime_error(e.what());
        }
    }
    return PriceWithAnalytic(product, market, dates, "c++_analytic_bs");
}

[[nodiscard]] py::dict PriceVanillaBinomial(const double spot,
                                            const double strike,
                                            const double vol,
                                            const double rate,
                                            const double div,
                                            const double tau_years,
                                            const bool is_call,
                                            const std::string& exercise,
                                            const std::size_t n_steps) {
    RequirePositiveSpotStrike(spot, strike);
    RequireNonNegVol(vol);

    const std::string ex = [&] {
        std::string s = exercise;
        std::transform(
                s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }();

    numeraire::ExerciseStyle style = numeraire::ExerciseStyle::kEuropean;
    if (ex == "american") {
        style = numeraire::ExerciseStyle::kAmerican;
    } else if (ex != "european" && !ex.empty()) {
        throw py::value_error("exercise must be 'european' or 'american'");
    }

    const LabDates dates = MakeLabDates(tau_years);
    const numeraire::products::VanillaEquityOptionProduct product(
            "LAB",
            is_call ? numeraire::OptionType::kCall : numeraire::OptionType::kPut,
            style,
            strike,
            dates.valuation,
            dates.expiry);
    const FlatMarket market(dates.valuation, spot, rate, div, vol);

    try {
        const std::size_t steps =
                n_steps == 0 ? numeraire::pricers::BinomialBlackScholesEquityPricer::kFallbackSteps : n_steps;
        const auto pricer = std::make_unique<numeraire::pricers::BinomialBlackScholesEquityPricer>(steps);
        py::dict out = ResultToDict(pricer->Price(product, market), "c++_binomial_crr", dates);
        out["n_steps"] = static_cast<int>(pricer->NSteps());
        return out;
    } catch (const numeraire::ValidationError& e) {
        throw py::value_error(e.what());
    } catch (const numeraire::NumeraireException& e) {
        throw std::runtime_error(e.what());
    }
}

[[nodiscard]] py::dict PriceVanillaMonteCarlo(const double spot,
                                              const double strike,
                                              const double vol,
                                              const double rate,
                                              const double div,
                                              const double tau_years,
                                              const bool is_call,
                                              const std::size_t num_paths,
                                              const std::uint64_t seed) {
    RequirePositiveSpotStrike(spot, strike);
    RequireNonNegVol(vol);
    if (num_paths == 0) {
        throw py::value_error("num_paths must be positive");
    }

    const LabDates dates = MakeLabDates(tau_years);
    const numeraire::products::VanillaEquityOptionProduct product(
            "LAB",
            is_call ? numeraire::OptionType::kCall : numeraire::OptionType::kPut,
            numeraire::ExerciseStyle::kEuropean,
            strike,
            dates.valuation,
            dates.expiry);
    const FlatMarket market(dates.valuation, spot, rate, div, vol);

    try {
        const auto pricer =
                std::make_unique<numeraire::pricers::MonteCarloGbmEuropeanPricer>(num_paths, seed);
        py::dict out = ResultToDict(pricer->Price(product, market), "c++_monte_carlo_gbm", dates);
        out["mc_paths"] = static_cast<int>(pricer->NumPaths());
        out["mc_seed"] = static_cast<long long>(pricer->Seed());
        return out;
    } catch (const numeraire::ValidationError& e) {
        throw py::value_error(e.what());
    } catch (const numeraire::NumeraireException& e) {
        throw std::runtime_error(e.what());
    }
}

/// Soft cap for lab / audit tree drawing (full node list grows as O(n²)).
constexpr std::size_t kMaxCrrTreeDumpSteps = 12;

[[nodiscard]] py::dict DumpCrrTree(const double spot,
                                   const double strike,
                                   const double vol,
                                   const double rate,
                                   const double div,
                                   const double tau_years,
                                   const bool is_call,
                                   const std::string& exercise,
                                   const std::size_t n_steps) {
    RequirePositiveSpotStrike(spot, strike);
    RequireNonNegVol(vol);
    if (tau_years < 0.0) {
        throw py::value_error("tau_years must be non-negative");
    }
    if (n_steps == 0 || n_steps > kMaxCrrTreeDumpSteps) {
        throw py::value_error("n_steps for tree dump must be in 1.." + std::to_string(kMaxCrrTreeDumpSteps));
    }

    const std::string ex = [&] {
        std::string s = exercise;
        std::transform(
                s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }();
    numeraire::ExerciseStyle style = numeraire::ExerciseStyle::kEuropean;
    if (ex == "american") {
        style = numeraire::ExerciseStyle::kAmerican;
    } else if (ex != "european" && !ex.empty()) {
        throw py::value_error("exercise must be 'european' or 'american'");
    }

    const auto dump = numeraire::quant::CoxRossRubinsteinVanillaTree(
            is_call ? numeraire::OptionType::kCall : numeraire::OptionType::kPut,
            style,
            spot,
            strike,
            rate,
            div,
            vol,
            tau_years,
            n_steps);

    py::list nodes;
    for (const auto& node : dump.nodes) {
        py::dict n;
        n["step"] = node.step;
        n["up_moves"] = node.up_moves;
        n["spot"] = node.spot;
        n["intrinsic"] = node.intrinsic;
        n["continuation"] = node.continuation;
        n["value"] = node.value;
        n["early_exercise"] = node.early_exercise;
        nodes.append(n);
    }

    py::dict out;
    out["ok"] = true;
    out["n_steps"] = static_cast<int>(dump.n_steps);
    out["dt"] = dump.dt;
    out["u"] = dump.u;
    out["d"] = dump.d;
    out["p_up"] = dump.p_up;
    out["discount"] = dump.discount;
    out["npv"] = dump.npv;
    out["nodes"] = nodes;
    return out;
}

[[nodiscard]] py::dict PriceAssetOrNothing(const double spot,
                                           const double strike,
                                           const double vol,
                                           const double rate,
                                           const double div,
                                           const double tau_years,
                                           const bool is_call) {
    RequirePositiveSpotStrike(spot, strike);
    RequireNonNegVol(vol);
    const LabDates dates = MakeLabDates(tau_years);
    const numeraire::products::EquityAssetOrNothingProduct product(
            "LAB",
            is_call ? numeraire::OptionType::kCall : numeraire::OptionType::kPut,
            numeraire::ExerciseStyle::kEuropean,
            strike,
            dates.valuation,
            dates.expiry);
    const FlatMarket market(dates.valuation, spot, rate, div, vol);
    return PriceWithAnalytic(product, market, dates, "c++_analytic_aon");
}

[[nodiscard]] py::dict PriceCashOrNothing(const double spot,
                                          const double strike,
                                          const double vol,
                                          const double rate,
                                          const double div,
                                          const double tau_years,
                                          const bool is_call,
                                          const double cash_payout) {
    RequirePositiveSpotStrike(spot, strike);
    RequireNonNegVol(vol);
    if (!(cash_payout > 0.0)) {
        throw py::value_error("cash_payout must be positive");
    }
    const LabDates dates = MakeLabDates(tau_years);
    const numeraire::products::EquityCashOrNothingProduct product(
            "LAB",
            is_call ? numeraire::OptionType::kCall : numeraire::OptionType::kPut,
            numeraire::ExerciseStyle::kEuropean,
            strike,
            cash_payout,
            dates.valuation,
            dates.expiry);
    const FlatMarket market(dates.valuation, spot, rate, div, vol);
    return PriceWithAnalytic(product, market, dates, "c++_analytic_con");
}

[[nodiscard]] py::dict PriceEquityForward(
        const double spot, const double forward_price, const double rate, const double div, const double tau_years) {
    RequirePositiveSpotStrike(spot, forward_price);
    const LabDates dates = MakeLabDates(tau_years);
    const numeraire::products::EquityForwardProduct product("LAB", forward_price, dates.valuation, dates.expiry);
    // Vol unused for forwards; FlatMarket still needs a value.
    const FlatMarket market(dates.valuation, spot, rate, div, 0.0);
    return PriceWithAnalytic(product, market, dates, "c++_analytic_forward");
}

constexpr std::size_t kMaxLabSimPaths = 100;
constexpr std::size_t kMaxLabSimIntervals = 250;
constexpr int kMinLabHorizonDays = 7;
constexpr int kMaxLabHorizonDays = 730;

[[nodiscard]] std::string NormalizeModel(std::string model) {
    std::transform(model.begin(), model.end(), model.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return model;
}

/// Uniform toy grid for Simulation Lab (not the production CCR schedule).
/// `n_intervals` equal steps over `horizon_days` (Act/365 year fractions).
[[nodiscard]] numeraire::simulation::ExposureTimeGrid MakeUniformLabGrid(const numeraire::schedule::Date& valuation,
                                                                         const int horizon_days,
                                                                         const std::size_t n_intervals) {
    numeraire::simulation::ExposureTimeGrid grid;
    grid.valuation_date = valuation;
    grid.nodes.reserve(n_intervals + 1U);
    const double horizon_years = static_cast<double>(horizon_days) / 365.0;
    for (std::size_t k = 0; k <= n_intervals; ++k) {
        const double w = static_cast<double>(k) / static_cast<double>(n_intervals);
        const int dte = static_cast<int>(std::llround(w * static_cast<double>(horizon_days)));
        numeraire::simulation::ExposureGridNode node;
        node.date = numeraire::schedule::AddCalendarDays(valuation, dte);
        node.target_dte_days = dte;
        node.year_fraction = w * horizon_years;
        node.pillar_id = (k == 0) ? "t0" : ("t" + std::to_string(k));
        grid.nodes.push_back(std::move(node));
    }
    // Guarantee strictly increasing year_fraction for the evolution kernel.
    for (std::size_t k = 1; k < grid.nodes.size(); ++k) {
        if (grid.nodes[k].year_fraction <= grid.nodes[k - 1].year_fraction) {
            grid.nodes[k].year_fraction =
                    grid.nodes[k - 1].year_fraction + horizon_years / static_cast<double>(n_intervals);
        }
    }
    return grid;
}

/// Sandbox path simulation for Simulation Lab.
/// Uniform time grid over a fixed horizon (toy / educational — not prod schedule).
[[nodiscard]] py::dict SimulatePaths(const std::string& model,
                                     const double spot,
                                     const double rate,
                                     const double div,
                                     const double vol,
                                     const std::size_t n_paths,
                                     const std::uint64_t seed,
                                     const int horizon_days,
                                     const std::size_t n_intervals) {
    if (!(spot > 0.0)) {
        throw py::value_error("spot must be positive");
    }
    if (vol < 0.0) {
        throw py::value_error("vol must be non-negative");
    }
    if (n_paths == 0 || n_paths > kMaxLabSimPaths) {
        throw py::value_error("n_paths must be in 1.." + std::to_string(kMaxLabSimPaths));
    }
    if (horizon_days < kMinLabHorizonDays || horizon_days > kMaxLabHorizonDays) {
        throw py::value_error("horizon_days must be in " + std::to_string(kMinLabHorizonDays) + ".." +
                              std::to_string(kMaxLabHorizonDays));
    }
    if (n_intervals == 0 || n_intervals > kMaxLabSimIntervals) {
        throw py::value_error("n_intervals must be in 1.." + std::to_string(kMaxLabSimIntervals));
    }

    const std::string model_key = NormalizeModel(model);
    if (model_key == "bachelier" || model_key == "hull_white" || model_key == "hull-white" || model_key == "heston") {
        throw py::value_error("model '" + model_key + "' is stubbed — only 'gbm' is wired in Quant Lab");
    }
    if (model_key != "gbm") {
        throw py::value_error("unknown model '" + model + "' (supported: gbm; stubs: bachelier, hull_white, heston)");
    }

    try {
        const numeraire::schedule::Date valuation{.year = 2026, .month = 1, .day = 2};
        const numeraire::simulation::ExposureTimeGrid grid = MakeUniformLabGrid(valuation, horizon_days, n_intervals);

        numeraire::simulation::ScenarioBuffer buffer(1, grid.NumSteps(), n_paths);
        numeraire::simulation::MersenneTwisterEngine engine(seed);
        const numeraire::simulation::SingleFactorGbmSpec spec{
                .spot = spot, .risk_free_rate = rate, .dividend_yield = div, .volatility = vol};
        numeraire::simulation::EvolveSingleFactorGbm(buffer, grid, spec, engine);

        py::list times;
        py::list days;
        for (const auto& node : grid.nodes) {
            times.append(node.year_fraction);
            days.append(node.target_dte_days);
        }

        py::list paths;
        for (std::size_t p = 0; p < n_paths; ++p) {
            py::list series;
            for (std::size_t step = 0; step < grid.NumSteps(); ++step) {
                series.append(buffer.At(0, step, p));
            }
            paths.append(series);
        }

        py::dict out;
        out["ok"] = true;
        out["model"] = "gbm";
        out["engine"] = "c++_evolve_single_factor_gbm";
        out["grid_name"] = "uniform_lab";
        out["horizon_days"] = horizon_days;
        out["n_paths"] = static_cast<int>(n_paths);
        out["n_steps"] = static_cast<int>(grid.NumSteps());
        out["n_intervals"] = static_cast<int>(n_intervals);
        out["seed"] = static_cast<int>(seed);
        out["spot"] = spot;
        out["rate"] = rate;
        out["div"] = div;
        out["vol"] = vol;
        out["times"] = times;
        out["days"] = days;
        out["paths"] = paths;
        return out;
    } catch (const numeraire::ValidationError& e) {
        throw py::value_error(e.what());
    } catch (const numeraire::NumeraireException& e) {
        throw std::runtime_error(e.what());
    }
}

}  // namespace

PYBIND11_MODULE(numeraire_cpp, m) {
    m.doc() = "Numeraire++ Python bindings for Quant Lab (pricing + path simulation).";
    m.def("price_vanilla",
          &PriceVanilla,
          py::arg("spot"),
          py::arg("strike"),
          py::arg("vol"),
          py::arg("rate"),
          py::arg("div"),
          py::arg("tau_years"),
          py::arg("is_call") = true,
          py::arg("exercise") = "european",
          py::arg("n_steps") = 0,
          R"pbdoc(
Price a vanilla equity option with the production C++ pricers.

European → analytic Black–Scholes. American → CRR binomial.
)pbdoc");
    m.def("price_vanilla_binomial",
          &PriceVanillaBinomial,
          py::arg("spot"),
          py::arg("strike"),
          py::arg("vol"),
          py::arg("rate"),
          py::arg("div"),
          py::arg("tau_years"),
          py::arg("is_call") = true,
          py::arg("exercise") = "european",
          py::arg("n_steps") = 200,
          R"pbdoc(
Price a vanilla equity option with the C++ Cox–Ross–Rubinstein tree.

Works for European and American exercise (Quant Lab comparison engine).
)pbdoc");
    m.def("price_vanilla_mc",
          &PriceVanillaMonteCarlo,
          py::arg("spot"),
          py::arg("strike"),
          py::arg("vol"),
          py::arg("rate"),
          py::arg("div"),
          py::arg("tau_years"),
          py::arg("is_call") = true,
          py::arg("num_paths") = 10000,
          py::arg("seed") = 42,
          R"pbdoc(
European vanilla via Monte Carlo GBM (single exact step to T).

Returns NPV and diagnostics (mc_paths, mc_seed, mc_std_err). No greeks.
)pbdoc");
    m.def("price_asset_or_nothing",
          &PriceAssetOrNothing,
          py::arg("spot"),
          py::arg("strike"),
          py::arg("vol"),
          py::arg("rate"),
          py::arg("div"),
          py::arg("tau_years"),
          py::arg("is_call") = true,
          R"pbdoc(European asset-or-nothing via analytic Black–Scholes.)pbdoc");
    m.def("price_cash_or_nothing",
          &PriceCashOrNothing,
          py::arg("spot"),
          py::arg("strike"),
          py::arg("vol"),
          py::arg("rate"),
          py::arg("div"),
          py::arg("tau_years"),
          py::arg("is_call") = true,
          py::arg("cash_payout") = 1.0,
          R"pbdoc(European cash-or-nothing / digital via analytic Black–Scholes.)pbdoc");
    m.def("price_equity_forward",
          &PriceEquityForward,
          py::arg("spot"),
          py::arg("forward_price"),
          py::arg("rate"),
          py::arg("div"),
          py::arg("tau_years"),
          R"pbdoc(Equity forward NPV: S e^{-qτ} − K e^{-rτ}.)pbdoc");
    m.def("dump_crr_tree",
          &DumpCrrTree,
          py::arg("spot"),
          py::arg("strike"),
          py::arg("vol"),
          py::arg("rate"),
          py::arg("div"),
          py::arg("tau_years"),
          py::arg("is_call") = true,
          py::arg("exercise") = "american",
          py::arg("n_steps") = 3,
          R"pbdoc(
Dump CRR tree nodes for educational / audit drawing (n_steps in 1..12).

Each node: step, up_moves, spot, intrinsic, continuation, value, early_exercise.
)pbdoc");
    m.def("simulate_paths",
          &SimulatePaths,
          py::arg("model") = "gbm",
          py::arg("spot") = 100.0,
          py::arg("rate") = 0.04,
          py::arg("div") = 0.0,
          py::arg("vol") = 0.20,
          py::arg("n_paths") = 30,
          py::arg("seed") = 42,
          py::arg("horizon_days") = 90,
          py::arg("n_intervals") = 48,
          R"pbdoc(
Simulate sandbox GBM paths on a uniform toy time grid (not the prod CCR schedule).

horizon_days: fixed lab horizon (e.g. 14 / 30 / 90 / 180 / 365).
n_intervals: equal steps across that horizon.
Stubs: bachelier, hull_white, heston.
)pbdoc");

    m.def("discount_factor_from_continuous_zero",
          &numeraire::quant::DiscountFactorFromContinuousZero,
          py::arg("zero_rate"),
          py::arg("time_years"),
          "DF = exp(-z * t) for continuous zero.");
    m.def("continuous_zero_from_discount_factor",
          &numeraire::quant::ContinuousZeroFromDiscountFactor,
          py::arg("discount_factor"),
          py::arg("time_years"),
          "z = -ln(DF) / t.");
}
