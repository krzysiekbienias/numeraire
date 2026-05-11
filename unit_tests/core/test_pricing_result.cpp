#include <gtest/gtest.h>

#include <numeraire/core/pricing_result.hpp>
#include <numeraire/enums/currency.hpp>

TEST(PricingResultTest, DefaultConstructedHoldsZeroNpv) {
    const numeraire::core::PricingResult r{};
    EXPECT_EQ(r.npv, 0.0);
    EXPECT_FALSE(r.greeks.has_value());
    EXPECT_FALSE(r.metadata.has_value());
}

TEST(PricingResultTest, CanSetGreeksAndMetadata) {
    numeraire::core::PricingResult r{.npv = 3.14};
    numeraire::core::PricingGreeks g{};
    g.delta = 0.5;
    g.vega = 0.02;
    r.greeks = g;

    numeraire::core::PricingMetadata m{};
    m.as_of = "2026-05-10";
    m.result_currency = numeraire::Currency::kUsd;
    m.diagnostics = "smoke";
    r.metadata = m;

    EXPECT_EQ(r.npv, 3.14);
    ASSERT_TRUE(r.greeks.has_value());
    EXPECT_EQ(r.greeks->delta, 0.5);
    EXPECT_EQ(r.greeks->vega, 0.02);
    ASSERT_TRUE(r.metadata.has_value());
    EXPECT_EQ(r.metadata->as_of, "2026-05-10");
    EXPECT_EQ(r.metadata->result_currency, numeraire::Currency::kUsd);
}
