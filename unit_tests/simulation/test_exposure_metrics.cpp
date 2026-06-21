#include <gtest/gtest.h>

#include <numeraire/schedule/date.hpp>
#include <numeraire/simulation/exposure_metrics.hpp>
#include <numeraire/simulation/exposure_time_grid.hpp>
#include <numeraire/simulation/leg_path_pv_buffer.hpp>

#include <string>
#include <vector>

namespace {

using numeraire::schedule::ParseIsoDate;
using numeraire::simulation::ComputeLegExposureMetrics;
using numeraire::simulation::ExposureGridNode;
using numeraire::simulation::ExposureTimeGrid;
using numeraire::simulation::LegExposureIdentity;
using numeraire::simulation::LegExposureMetrics;
using numeraire::simulation::LegPathPvBuffer;

ExposureTimeGrid SimpleGrid() {
    ExposureTimeGrid grid;
    grid.valuation_date = ParseIsoDate("2026-06-15");
    grid.nodes = {
            ExposureGridNode{.date = ParseIsoDate("2026-06-15"), .year_fraction = 0.0, .target_dte_days = 0, .pillar_id = "ASOF"},
            ExposureGridNode{.date = ParseIsoDate("2026-07-15"), .year_fraction = 0.082191780821917804, .target_dte_days = 30, .pillar_id = "M1"},
    };
    return grid;
}

}  // namespace

TEST(ExposureMetricsTest, ComputesEeAndPfeFromLegPv) {
    const ExposureTimeGrid grid = SimpleGrid();
    LegPathPvBuffer leg_pv(1, grid.NumSteps(), 4);

    // step 0: exposures 0, 10, 20, 30 -> ee=15, pfe95=28.5, pfe97=29.1
    leg_pv.At(0, 0, 0) = -5.0;
    leg_pv.At(0, 0, 1) = 10.0;
    leg_pv.At(0, 0, 2) = 20.0;
    leg_pv.At(0, 0, 3) = 30.0;

    // step 1: all zero exposure
    for (std::size_t path = 0; path < leg_pv.NumPaths(); ++path) {
        leg_pv.At(0, 1, path) = -1.0;
    }

    LegExposureIdentity leg;
    leg.leg_id = "LEG_1";
    leg.trade_id = "TRD_1";
    const std::vector<LegExposureIdentity> legs{leg};

    std::vector<LegExposureMetrics> metrics;
    ComputeLegExposureMetrics(leg_pv, legs, grid, metrics);

    ASSERT_EQ(metrics.size(), 2U);
    EXPECT_EQ(metrics[0].leg_id, "LEG_1");
    EXPECT_EQ(metrics[0].trade_id, "TRD_1");
    EXPECT_EQ(metrics[0].pillar_id, "ASOF");
    EXPECT_DOUBLE_EQ(metrics[0].ee, 15.0);
    EXPECT_DOUBLE_EQ(metrics[0].pfe_95, 28.5);
    EXPECT_DOUBLE_EQ(metrics[0].pfe_97, 29.1);

    EXPECT_EQ(metrics[1].pillar_id, "M1");
    EXPECT_DOUBLE_EQ(metrics[1].ee, 0.0);
    EXPECT_DOUBLE_EQ(metrics[1].pfe_95, 0.0);
    EXPECT_DOUBLE_EQ(metrics[1].pfe_97, 0.0);
}
