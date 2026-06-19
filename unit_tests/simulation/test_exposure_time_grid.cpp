#include <gtest/gtest.h>

#include <filesystem>
#include <numeraire/schedule/date.hpp>
#include <numeraire/simulation/exposure_grid_config.hpp>
#include <numeraire/simulation/exposure_time_grid.hpp>

namespace fs = std::filesystem;

namespace {

using numeraire::schedule::Act365FixedYearFraction;
using numeraire::schedule::AddCalendarDays;
using numeraire::schedule::Date;
using numeraire::schedule::ParseIsoDate;
using numeraire::simulation::BuildExposureTimeGrid;
using numeraire::simulation::LoadExposureGridConfig;

[[nodiscard]] numeraire::simulation::ExposureGridConfig TestConfig() {
    numeraire::simulation::ExposureGridConfig cfg;
    cfg.conventions.include_valuation_date = true;
    cfg.conventions.clip_to_book_max_expiry = true;
    cfg.conventions.min_horizon_days = 365;
    cfg.exposure_pillars = {
            {.id = "W1", .target_dte_days = 7, .tier = "core"},
            {.id = "M1", .target_dte_days = 30, .tier = "core"},
            {.id = "M3", .target_dte_days = 90, .tier = "core"},
            {.id = "Y1", .target_dte_days = 365, .tier = "core"},
            {.id = "Y2", .target_dte_days = 730, .tier = "wing"},
    };
    return cfg;
}

TEST(ExposureTimeGridTest, FirstNodeIsValuationWithZeroTau) {
    const Date as_of = ParseIsoDate("2026-01-02");
    const auto grid = BuildExposureTimeGrid(TestConfig(), as_of, std::nullopt);
    ASSERT_GE(grid.NumSteps(), 1U);
    EXPECT_EQ(grid.valuation_date.year, as_of.year);
    EXPECT_EQ(grid.nodes.front().date.day, as_of.day);
    EXPECT_NEAR(grid.nodes.front().year_fraction, 0.0, 1.0e-12);
    EXPECT_EQ(grid.nodes.front().pillar_id, "ASOF");
}

TEST(ExposureTimeGridTest, NodesAreMonotonicWithAct365Fractions) {
    const Date as_of = ParseIsoDate("2026-01-02");
    const auto grid = BuildExposureTimeGrid(TestConfig(), as_of, std::nullopt);
    ASSERT_GE(grid.NumSteps(), 2U);
    for (std::size_t i = 1; i < grid.NumSteps(); ++i) {
        EXPECT_LT(grid.nodes[i - 1].year_fraction, grid.nodes[i].year_fraction);
        EXPECT_NEAR(grid.nodes[i].year_fraction,
                    Act365FixedYearFraction(as_of, grid.nodes[i].date), 1.0e-12);
    }
}

TEST(ExposureTimeGridTest, PillarDatesMatchCalendarAdvance) {
    const Date as_of = ParseIsoDate("2026-01-02");
    const auto grid = BuildExposureTimeGrid(TestConfig(), as_of, std::nullopt);
    const Date expected_m1 = AddCalendarDays(as_of, 30);
    bool found_m1 = false;
    for (const auto& node : grid.nodes) {
        if (node.pillar_id == "M1") {
            found_m1 = true;
            EXPECT_EQ(node.date.year, expected_m1.year);
            EXPECT_EQ(node.date.month, expected_m1.month);
            EXPECT_EQ(node.date.day, expected_m1.day);
        }
    }
    EXPECT_TRUE(found_m1);
}

TEST(ExposureTimeGridTest, ClipsToBookMaxExpiry) {
    const Date as_of = ParseIsoDate("2026-01-02");
    const Date max_expiry = ParseIsoDate("2026-03-01");
    const auto grid = BuildExposureTimeGrid(TestConfig(), as_of, max_expiry);
    for (const auto& node : grid.nodes) {
        EXPECT_FALSE(node.date.year > max_expiry.year ||
                     (node.date.year == max_expiry.year && node.date.month > max_expiry.month) ||
                     (node.date.year == max_expiry.year && node.date.month == max_expiry.month &&
                      node.date.day > max_expiry.day));
    }
    EXPECT_LT(grid.nodes.back().year_fraction,
              Act365FixedYearFraction(as_of, AddCalendarDays(max_expiry, 1)));
}

TEST(ExposureTimeGridTest, BuiltFromCommittedConfigFile) {
    const fs::path path = fs::path(NUMERAIRE_SOURCE_DIR) / "configs" / "simulation_exposure_grid.json";
    const auto cfg = LoadExposureGridConfig(path);
    const Date as_of = ParseIsoDate("2026-05-15");
    const auto grid = BuildExposureTimeGrid(cfg, as_of, std::nullopt);
    EXPECT_GE(grid.NumSteps(), cfg.exposure_pillars.size());
    EXPECT_NEAR(grid.nodes.front().year_fraction, 0.0, 1.0e-12);
}

}  // namespace
