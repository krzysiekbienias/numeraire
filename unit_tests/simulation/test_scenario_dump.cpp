#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <numeraire/schedule/date.hpp>
#include <numeraire/simulation/exposure_time_grid.hpp>
#include <numeraire/simulation/scenario_buffer.hpp>
#include <numeraire/simulation/scenario_dump.hpp>
#include <numeraire/utils/exception.hpp>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

using numeraire::schedule::ParseIsoDate;
using numeraire::simulation::BuildExposureTimeGrid;
using numeraire::simulation::DumpScenarioPathsCsv;
using numeraire::simulation::DumpScenarioPathsOptions;
using numeraire::simulation::ExposureGridConfig;
using numeraire::simulation::ScenarioBuffer;

[[nodiscard]] auto SimpleGrid() {
    ExposureGridConfig cfg;
    cfg.conventions.include_valuation_date = true;
    cfg.exposure_pillars = {
            {.id = "M1", .target_dte_days = 30, .tier = "core"},
            {.id = "M3", .target_dte_days = 90, .tier = "core"},
    };
    return BuildExposureTimeGrid(cfg, ParseIsoDate("2026-01-02"), std::nullopt);
}

[[nodiscard]] std::string ReadFile(const fs::path& path) {
    std::ifstream in(path);
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

TEST(ScenarioDumpTest, WritesCsvWithPathCap) {
    const auto grid = SimpleGrid();
    ScenarioBuffer buffer(1, grid.NumSteps(), 8);
    for (std::size_t step = 0; step < grid.NumSteps(); ++step) {
        for (std::size_t path = 0; path < buffer.NumPaths(); ++path) {
            buffer.At(0, step, path) = static_cast<double>((step * 10U) + path);
        }
    }

    const fs::path out = fs::temp_directory_path() / "numeraire_scenario_dump_test.csv";
    DumpScenarioPathsOptions options{.factor = 0, .max_paths = 3};
    DumpScenarioPathsCsv(out, buffer, grid, options);

    const std::string text = ReadFile(out);
    EXPECT_TRUE(text.starts_with("path,step,year_fraction,value\n"));

    std::size_t line_count = 0;
    for (char ch : text) {
        if (ch == '\n') {
            ++line_count;
        }
    }
    EXPECT_EQ(line_count, 1U + (3U * grid.NumSteps()));

    EXPECT_NE(text.find("0,0,0,"), std::string::npos);
    EXPECT_NE(text.find("2,"), std::string::npos);
    EXPECT_EQ(text.find("3,"), std::string::npos);

    fs::remove(out);
}

TEST(ScenarioDumpTest, RejectsDimensionMismatch) {
    const auto grid = SimpleGrid();
    ScenarioBuffer buffer(1, grid.NumSteps() + 1U, 4);
    const fs::path out = fs::temp_directory_path() / "numeraire_scenario_dump_bad.csv";
    EXPECT_THROW(DumpScenarioPathsCsv(out, buffer, grid), numeraire::ValidationError);
}

}  // namespace
