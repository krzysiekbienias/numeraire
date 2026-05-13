#include <gtest/gtest.h>

#include <numeraire/core/ipricer.hpp>
#include <numeraire/enums/model_type.hpp>
#include <numeraire/enums/pricing_engine_type.hpp>
#include <numeraire/pricers/analytic_black_scholes_equity_pricer.hpp>
#include <numeraire/pricers/pricer_factory.hpp>
#include <numeraire/utils/exception.hpp>

TEST(PricerFactoryTest, AnalyticBlackScholesReturnsPricerWithMatchingEngineKind) {
    auto pricer = numeraire::pricers::PricerFactory::Make(numeraire::PricingEngineType::kAnalytic,
                                                          numeraire::ModelType::kBlackScholes);
    ASSERT_NE(pricer, nullptr);
    EXPECT_EQ(pricer->EngineKind(), numeraire::PricingEngineType::kAnalytic);
    // Sanity: same concrete type users get when constructing directly
    const numeraire::pricers::AnalyticBlackScholesEquityPricer direct{};
    EXPECT_EQ(pricer->EngineKind(), direct.EngineKind());
}

TEST(PricerFactoryTest, AnalyticNonBlackScholesThrows) {
    EXPECT_THROW(static_cast<void>(numeraire::pricers::PricerFactory::Make(numeraire::PricingEngineType::kAnalytic,
                                                                           numeraire::ModelType::kHeston)),
                 numeraire::ValidationError);
    EXPECT_THROW(static_cast<void>(numeraire::pricers::PricerFactory::Make(numeraire::PricingEngineType::kAnalytic,
                                                                           numeraire::ModelType::kLocalVolatility)),
                 numeraire::ValidationError);
}

TEST(PricerFactoryTest, NonAnalyticEngineThrows) {
    EXPECT_THROW(static_cast<void>(numeraire::pricers::PricerFactory::Make(numeraire::PricingEngineType::kMonteCarlo,
                                                                           numeraire::ModelType::kBlackScholes)),
                 numeraire::ValidationError);
    EXPECT_THROW(static_cast<void>(numeraire::pricers::PricerFactory::Make(numeraire::PricingEngineType::kBinomialTree,
                                                                           numeraire::ModelType::kBlackScholes)),
                 numeraire::ValidationError);
    EXPECT_THROW(static_cast<void>(numeraire::pricers::PricerFactory::Make(
                         numeraire::PricingEngineType::kFiniteDifference, numeraire::ModelType::kBlackScholes)),
                 numeraire::ValidationError);
}
