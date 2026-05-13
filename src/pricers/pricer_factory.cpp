#include <numeraire/pricers/pricer_factory.hpp>

#include <numeraire/pricers/analytic_black_scholes_equity_pricer.hpp>
#include <numeraire/utils/exception.hpp>

#include <memory>

namespace numeraire::pricers {

namespace {

[[noreturn]] void ThrowUnsupported(const PricingEngineType engine, const ModelType model) {
    static_cast<void>(engine);
    static_cast<void>(model);
    throw ValidationError(
            "PricerFactory::Make: unsupported PricingEngineType / ModelType pair (extend "
            "pricer_factory.cpp when adding pricers).");
}

}  // namespace

std::unique_ptr<core::IPricer> PricerFactory::Make(const PricingEngineType engine, const ModelType model) {
    switch (engine) {
    case PricingEngineType::kAnalytic:
        switch (model) {
        case ModelType::kBlackScholes:
            return std::make_unique<AnalyticBlackScholesEquityPricer>();
        default:
            ThrowUnsupported(engine, model);
        }
    case PricingEngineType::kMonteCarlo:
    case PricingEngineType::kBinomialTree:
    case PricingEngineType::kFiniteDifference:
    default:
        ThrowUnsupported(engine, model);
    }
}

}  // namespace numeraire::pricers
