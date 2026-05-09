#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <numeraire/utils/config.hpp>
#include <numeraire/utils/exception.hpp>
#include <string>

#include <nlohmann/json.hpp>

#ifndef NUMERAIRE_SOURCE_DIR
#    error "NUMERAIRE_SOURCE_DIR must be set by CMake (see unit_tests/CMakeLists.txt)."
#endif

namespace {

[[nodiscard]] std::filesystem::path DefaultCommittedConfigPath() {
    return std::filesystem::path(NUMERAIRE_SOURCE_DIR) / "configs/default.json";
}

[[nodiscard]] std::filesystem::path UniqueTempDir() {
    using namespace std::chrono;
    const auto id = duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
    return std::filesystem::temp_directory_path() / ("numeraire_config_ut_" + std::to_string(id));
}

}  // namespace

TEST(ConfigTest, MissingFileThrowsConfigError) {
    EXPECT_THROW((void)numeraire::utils::Config::Load("no_such_config_9d4e2f1a.json"), numeraire::ConfigError);
}

TEST(ConfigTest, InvalidJsonThrowsConfigError) {
    const auto dir = UniqueTempDir();
    std::filesystem::create_directories(dir);
    const auto path = dir / "bad.json";
    {
        std::ofstream out(path);
        out << "{ not json";
    }

    EXPECT_THROW((void)numeraire::utils::Config::Load(path), numeraire::ConfigError);
}

TEST(ConfigTest, LoadsDefaultCommittedConfig) {
    const auto cfg = numeraire::utils::Config::Load(DefaultCommittedConfigPath());
    EXPECT_EQ(cfg.RequireString("version"), "0.1.0");
    EXPECT_EQ(cfg.RequireString("logging.level"), "info");
    const nlohmann::json& rotation = cfg.RequireAt("logging.rotation");
    EXPECT_TRUE(rotation.is_object());
    EXPECT_EQ(rotation.at("max_files").get<int>(), 5);
}

TEST(ConfigTest, RequireAtMissingPathThrows) {
    const auto cfg = numeraire::utils::Config::Load(DefaultCommittedConfigPath());
    EXPECT_THROW((void)cfg.RequireAt("no.such.path"), numeraire::ConfigError);
}

TEST(ConfigTest, RequireStringRejectsNonString) {
    const auto cfg = numeraire::utils::Config::Load(DefaultCommittedConfigPath());
    EXPECT_THROW((void)cfg.RequireString("pricing.monte_carlo.default_paths"), numeraire::ConfigError);
}

TEST(ConfigTest, EmptyDottedSegmentThrows) {
    const auto dir = UniqueTempDir();
    std::filesystem::create_directories(dir);
    const auto path = dir / "dots.json";
    {
        std::ofstream out(path);
        out << R"({"a":{"b":1}})";
    }
    const auto cfg = numeraire::utils::Config::Load(path);
    EXPECT_THROW((void)cfg.RequireAt("a..b"), numeraire::ConfigError);
}
