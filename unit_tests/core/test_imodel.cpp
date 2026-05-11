#include <gtest/gtest.h>

#include <numeraire/core/imodel.hpp>

#include <cmath>
#include <limits>
#include <memory>

namespace {

class BlackScholesFlatVolModel final : public numeraire::core::IModel {
   public:
    explicit BlackScholesFlatVolModel(const double sigma) : sigma_(sigma) {}

    [[nodiscard]] numeraire::ModelType Kind() const override {
        return numeraire::ModelType::kBlackScholes;
    }

    [[nodiscard]] double FlatVolatility() const override { return sigma_; }

   private:
    double sigma_;
};

class HestonStubModel final : public numeraire::core::IModel {
   public:
    [[nodiscard]] numeraire::ModelType Kind() const override {
        return numeraire::ModelType::kHeston;
    }

    [[nodiscard]] double FlatVolatility() const override {
        return std::numeric_limits<double>::quiet_NaN();
    }
};

void ExpectBsThroughInterface(const numeraire::core::IModel& m) {
    EXPECT_EQ(m.Kind(), numeraire::ModelType::kBlackScholes);
    EXPECT_DOUBLE_EQ(m.FlatVolatility(), 0.25);
}

}  // namespace

TEST(IModelTest, BlackScholesExposesKindAndSigma) {
    const BlackScholesFlatVolModel m(0.25);
    ExpectBsThroughInterface(m);
}

TEST(IModelTest, PolymorphicCallThroughInterface) {
    const auto m = std::make_unique<BlackScholesFlatVolModel>(0.25);
    const numeraire::core::IModel& ref = *m;
    ExpectBsThroughInterface(ref);
}

TEST(IModelTest, HestonStubReturnsNanFlatVolUntilExtended) {
    const HestonStubModel m;
    EXPECT_EQ(m.Kind(), numeraire::ModelType::kHeston);
    EXPECT_TRUE(std::isnan(m.FlatVolatility()));
}
