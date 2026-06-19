#include <gtest/gtest.h>

#include <filesystem>
#include <numeraire/simulation/exposure_grid_config.hpp>

namespace fs = std::filesystem;

namespace {

using numeraire::simulation::ExposureGridConfigPathFromDefaults;
using numeraire::simulation::LoadExposureGridConfig;

TEST(ExposureGridConfigTest, LoadsCommittedDefaultFile) {
    const fs::path path = fs::path(NUMERAIRE_SOURCE_DIR) / "configs" / "simulation_exposure_grid.json";
    const auto cfg = LoadExposureGridConfig(path);
    EXPECT_EQ(cfg.schema_version, "1.0.0");
    EXPECT_EQ(cfg.name, "default_ccr_exposure_grid");
    EXPECT_TRUE(cfg.conventions.include_valuation_date);
    EXPECT_TRUE(cfg.conventions.clip_to_book_max_expiry);
    EXPECT_EQ(cfg.conventions.min_horizon_days, 365);
    EXPECT_GE(cfg.exposure_pillars.size(), 10U);
    EXPECT_EQ(cfg.exposure_pillars.front().id, "W1");
    EXPECT_EQ(cfg.exposure_pillars.front().target_dte_days, 7);
}

TEST(ExposureGridConfigTest, PathFromDefaultJson) {
    const fs::path defaults = fs::path(NUMERAIRE_SOURCE_DIR) / "configs" / "default.json";
    const auto rel = ExposureGridConfigPathFromDefaults(defaults);
    ASSERT_TRUE(rel.has_value());
    EXPECT_EQ(rel->string(), "configs/simulation_exposure_grid.json");
}

}  // namespace
