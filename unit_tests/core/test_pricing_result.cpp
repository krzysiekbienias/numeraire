#include <gtest/gtest.h>

#include <numeraire/core/pricing_result.hpp>
#include <numeraire/enums/currency.hpp>

TEST(PricingResultTest, DefaultConstructedHasNoNpvGreeksOrMetadata) {
    const numeraire::core::PricingResult r{};
    EXPECT_FALSE(r.Npv().has_value());
    EXPECT_FALSE(r.Greeks().has_value());
    EXPECT_FALSE(r.Metadata().has_value());
}

TEST(PricingResultTest, ExplicitConstructorSetsNpv) {
    const numeraire::core::PricingResult r{3.14};
    ASSERT_TRUE(r.Npv().has_value());
    EXPECT_DOUBLE_EQ(*r.Npv(), 3.14);
    EXPECT_FALSE(r.Greeks().has_value());
    EXPECT_FALSE(r.Metadata().has_value());
}

TEST(PricingResultTest, SetNpvCanPopulateAndClear) {
    numeraire::core::PricingResult r{};
    EXPECT_FALSE(r.Npv().has_value());

    r.SetNpv(42.0);
    ASSERT_TRUE(r.Npv().has_value());
    EXPECT_DOUBLE_EQ(*r.Npv(), 42.0);

    r.SetNpv(std::nullopt);
    EXPECT_FALSE(r.Npv().has_value());
}

TEST(PricingResultTest, SetGreeksAndMetadataPreservesNpvAndPopulatesNestedFields) {
    numeraire::core::PricingResult r{3.14};

    numeraire::core::PricingGreeks g{};
    g.delta = 0.5;
    g.vega = 0.02;
    r.SetGreeks(g);

    numeraire::core::PricingMetadata m{};
    m.as_of = "2026-05-10";
    m.result_currency = numeraire::Currency::kUsd;
    m.diagnostics = "smoke";
    r.SetMetadata(std::move(m));

    ASSERT_TRUE(r.Npv().has_value());
    EXPECT_DOUBLE_EQ(*r.Npv(), 3.14);

    ASSERT_TRUE(r.Greeks().has_value());
    ASSERT_TRUE(r.Greeks()->delta.has_value());
    ASSERT_TRUE(r.Greeks()->vega.has_value());
    EXPECT_DOUBLE_EQ(*r.Greeks()->delta, 0.5);
    EXPECT_DOUBLE_EQ(*r.Greeks()->vega, 0.02);

    ASSERT_TRUE(r.Metadata().has_value());
    ASSERT_TRUE(r.Metadata()->as_of.has_value());
    ASSERT_TRUE(r.Metadata()->result_currency.has_value());
    ASSERT_TRUE(r.Metadata()->diagnostics.has_value());
    EXPECT_EQ(*r.Metadata()->as_of, "2026-05-10");
    EXPECT_EQ(*r.Metadata()->result_currency, numeraire::Currency::kUsd);
    EXPECT_EQ(*r.Metadata()->diagnostics, "smoke");
}

TEST(PricingResultTest, MutableNpvSupportsEmplace) {
    numeraire::core::PricingResult r{};
    r.Npv().emplace(1.25);
    ASSERT_TRUE(r.Npv().has_value());
    EXPECT_DOUBLE_EQ(*r.Npv(), 1.25);
}
