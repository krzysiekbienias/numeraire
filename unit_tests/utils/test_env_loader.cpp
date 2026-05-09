#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <numeraire/utils/env_loader.hpp>
#include <numeraire/utils/exception.hpp>
#include <string>

namespace {

[[nodiscard]] std::filesystem::path UniqueTempDir() {
    using namespace std::chrono;
    const auto id = duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
    return std::filesystem::temp_directory_path() / ("numeraire_env_loader_ut_" + std::to_string(id));
}

}  // namespace

TEST(EnvLoaderTest, MissingFileIsNoOp) {
    numeraire::utils::EnvLoader env;
    env.LoadFromFile("this_path_should_not_exist_7f3a2c1b.env");
    EXPECT_FALSE(env.Get("ANY_KEY").has_value());
}

TEST(EnvLoaderTest, ParsesKeyValueAndComments) {
    const auto dir = UniqueTempDir();
    std::filesystem::create_directories(dir);
    const auto path = dir / "test.env";
    {
        std::ofstream out(path);
        out << "# comment\n";
        out << " FOO = bar baz \n";
        out << "EMPTY=\n";
    }

    numeraire::utils::EnvLoader env;
    env.LoadFromFile(path);
    ASSERT_TRUE(env.Get("FOO").has_value());
    EXPECT_EQ(*env.Get("FOO"), "bar baz");
    ASSERT_TRUE(env.Get("EMPTY").has_value());
    EXPECT_TRUE(env.Get("EMPTY")->empty());
}

TEST(EnvLoaderTest, BadLineThrowsValidationError) {
    const auto dir = UniqueTempDir();
    std::filesystem::create_directories(dir);
    const auto path = dir / "bad.env";
    {
        std::ofstream out(path);
        out << "not_a_key_value_line\n";
    }

    numeraire::utils::EnvLoader env;
    EXPECT_THROW(env.LoadFromFile(path), numeraire::ValidationError);
}

TEST(EnvLoaderTest, EmptyKeyThrowsValidationError) {
    const auto dir = UniqueTempDir();
    std::filesystem::create_directories(dir);
    const auto path = dir / "empty_key.env";
    {
        std::ofstream out(path);
        out << "=value\n";
    }

    numeraire::utils::EnvLoader env;
    EXPECT_THROW(env.LoadFromFile(path), numeraire::ValidationError);
}

#if !defined(_WIN32)

TEST(EnvLoaderTest, UnreadableExistingFileThrowsConfigError) {
    namespace fs = std::filesystem;
    const auto dir = UniqueTempDir();
    fs::create_directories(dir);
    const auto path = dir / "locked.env";
    {
        std::ofstream out(path);
        out << "K=v\n";
    }
    fs::permissions(path, fs::perms::none);

    numeraire::utils::EnvLoader env;
    EXPECT_THROW(env.LoadFromFile(path), numeraire::ConfigError);

    fs::permissions(path, fs::perms::owner_all);
}

class ScopedEnvVar {
public:
    explicit ScopedEnvVar(std::string key) : key_(std::move(key)) {
        const char* previous = std::getenv(key_.c_str());
        if (previous != nullptr) {
            had_previous_ = true;
            previous_value_ = previous;
        }
    }

    void Set(const char* value) { ::setenv(key_.c_str(), value, 1); }

    void Unset() { ::unsetenv(key_.c_str()); }

    ~ScopedEnvVar() {
        if (had_previous_) {
            ::setenv(key_.c_str(), previous_value_.c_str(), 1);
        } else {
            ::unsetenv(key_.c_str());
        }
    }

    ScopedEnvVar(const ScopedEnvVar&) = delete;
    ScopedEnvVar& operator=(const ScopedEnvVar&) = delete;

private:
    std::string key_;
    bool had_previous_{false};
    std::string previous_value_;
};

// real environment wins over .env
TEST(EnvLoaderTest, GetPrefersNonEmptyProcessEnvironmentOverFile) {
    const auto dir = UniqueTempDir();
    std::filesystem::create_directories(dir);
    const auto path = dir / "prec.env";
    {
        std::ofstream out(path);
        out << "NUMERAIRE_UT_ENV_PREC=from_file\n";
    }

    ScopedEnvVar guard("NUMERAIRE_UT_ENV_PREC");
    guard.Unset();

    numeraire::utils::EnvLoader env;
    env.LoadFromFile(path);
    ASSERT_TRUE(env.Get("NUMERAIRE_UT_ENV_PREC").has_value());
    EXPECT_EQ(*env.Get("NUMERAIRE_UT_ENV_PREC"), "from_file");

    guard.Set("from_os");
    ASSERT_TRUE(env.Get("NUMERAIRE_UT_ENV_PREC").has_value());
    EXPECT_EQ(*env.Get("NUMERAIRE_UT_ENV_PREC"), "from_os");
}

TEST(EnvLoaderTest, ApplyToEnvironmentSetsUnsetVars) {
    const auto dir = UniqueTempDir();
    std::filesystem::create_directories(dir);
    const auto path = dir / "apply.env";
    {
        std::ofstream out(path);
        out << "NUMERAIRE_UT_ENV_APPLY=applied_value\n";
    }

    ScopedEnvVar guard("NUMERAIRE_UT_ENV_APPLY");
    guard.Unset();

    numeraire::utils::EnvLoader env;
    env.LoadFromFile(path);
    env.ApplyToEnvironment();

    const char* v = std::getenv("NUMERAIRE_UT_ENV_APPLY");
    ASSERT_NE(v, nullptr);
    EXPECT_STREQ(v, "applied_value");
}

TEST(EnvLoaderTest, ApplyToEnvironmentSkipsWhenEnvAlreadySet) {
    const auto dir = UniqueTempDir();
    std::filesystem::create_directories(dir);
    const auto path = dir / "skip.env";
    {
        std::ofstream out(path);
        out << "NUMERAIRE_UT_ENV_SKIP=from_file\n";
    }

    ScopedEnvVar guard("NUMERAIRE_UT_ENV_SKIP");
    guard.Set("from_os");

    numeraire::utils::EnvLoader env;
    env.LoadFromFile(path);
    env.ApplyToEnvironment();

    const char* v = std::getenv("NUMERAIRE_UT_ENV_SKIP");
    ASSERT_NE(v, nullptr);
    EXPECT_STREQ(v, "from_os");
}

#endif  // !defined(_WIN32)
