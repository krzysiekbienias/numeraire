#include <gtest/gtest.h>

#include <numeraire/schedule/date.hpp>
#include <numeraire/simulation/path_pricing_rules.hpp>

namespace {

using numeraire::schedule::ParseIsoDate;
using numeraire::simulation::IsLegActiveOnGridNode;

}  // namespace

TEST(PathPricingRulesTest, GridNodeInactiveAfterExpiry) {
    const auto expiry = ParseIsoDate("2026-09-18");
    const auto before = ParseIsoDate("2026-07-15");
    const auto after = ParseIsoDate("2026-10-01");

    EXPECT_TRUE(IsLegActiveOnGridNode(before, expiry));
    EXPECT_TRUE(IsLegActiveOnGridNode(expiry, expiry));
    EXPECT_FALSE(IsLegActiveOnGridNode(after, expiry));
}
