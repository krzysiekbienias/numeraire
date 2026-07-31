#include <gtest/gtest.h>

#include <numeraire/simulation/exposure_metrics.hpp>
#include <numeraire/simulation/exposure_time_grid.hpp>
#include <numeraire/simulation/leg_exposure_dump.hpp>
#include <numeraire/simulation/leg_path_pv_buffer.hpp>

#include <fstream>
#include <sstream>
#include <string>

#include <filesystem>

namespace fs = std::filesystem;

namespace {

using numeraire::schedule::ParseIsoDate;
using numeraire::simulation::DumpLegExposurePathsCsv;
using numeraire::simulation::ExposureGridNode;
using numeraire::simulation::ExposureTimeGrid;
using numeraire::simulation::LegExposureIdentity;
using numeraire::simulation::LegPathPvBuffer;

ExposureTimeGrid SimpleGrid() {
    ExposureTimeGrid grid;
    grid.valuation_date = ParseIsoDate("2026-06-15");
    grid.nodes = {
            ExposureGridNode{.date = ParseIsoDate("2026-06-15"), .year_fraction = 0.0, .target_dte_days = 0, .pillar_id = "ASOF"},
    };
    return grid;
}

}  // namespace

TEST(LegExposureDumpTest, WritesLongFormCsv) {
    const ExposureTimeGrid grid = SimpleGrid();
    LegPathPvBuffer leg_pv(1, grid.NumSteps(), 2);
    leg_pv.At(0, 0, 0) = 12.0;
    leg_pv.At(0, 0, 1) = -3.0;

    LegExposureIdentity leg;
    leg.leg_id = "LEG_1";
    leg.trade_id = "TRD_1";
    const std::vector<LegExposureIdentity> legs{leg};

    const fs::path out = fs::temp_directory_path() / "numeraire_leg_exposure_dump_test.csv";
    DumpLegExposurePathsCsv(out, leg_pv, grid, legs, {.max_paths = 2});

    std::ifstream in(out);
    ASSERT_TRUE(in.good());
    std::string header;
    std::getline(in, header);
    EXPECT_EQ(header, "path,leg_id,trade_id,pillar_id,step,year_fraction,pv_total,exposure");

    std::string row0;
    std::getline(in, row0);
    EXPECT_EQ(row0, "0,LEG_1,TRD_1,ASOF,0,0,12,12");

    std::string row1;
    std::getline(in, row1);
    EXPECT_EQ(row1, "1,LEG_1,TRD_1,ASOF,0,0,-3,0");

    fs::remove(out);
}
