#include <gtest/gtest.h>

#include <cstdlib>
#include <numeraire/utils/log_level.hpp>
#include <numeraire/utils/logger.hpp>
#include <string>

namespace {

class LoggerTest : public ::testing::Test {
protected:
    void TearDown() override { numeraire::utils::Logger::Shutdown(); }
};

#if !defined(_WIN32)

class ScopedEnvVar {
public:
    ScopedEnvVar(const char* key, const char* value) : key_(key) {
        const char* previous = std::getenv(key);
        if (previous != nullptr) {
            had_previous_ = true;
            previous_value_ = previous;
        }
        ::setenv(key_.c_str(), value, 1);
    }

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

#endif  // !defined(_WIN32)

}  // namespace

TEST_F(LoggerTest, CoreBeforeInitThrows) {
    EXPECT_THROW(numeraire::utils::Logger::Core(), std::logic_error);
}

#if !defined(_WIN32)

TEST_F(LoggerTest, InitDefaultsToInfoWhenEnvUnset) {
    ::unsetenv("NUMERAIRE_LOG_LEVEL");
    numeraire::utils::Logger::Init();
    EXPECT_EQ(numeraire::utils::Logger::Core().level(), spdlog::level::info);
}

TEST_F(LoggerTest, InitReadsLogLevelFromEnvironment) {
    ScopedEnvVar guard("NUMERAIRE_LOG_LEVEL", "debug");
    numeraire::utils::Logger::Init();
    EXPECT_EQ(numeraire::utils::Logger::Core().level(), spdlog::level::debug);
}

TEST_F(LoggerTest, ExplicitLevelOverridesEnvironment) {
    ScopedEnvVar guard("NUMERAIRE_LOG_LEVEL", "error");
    numeraire::utils::Logger::Init(numeraire::utils::LogLevel::kTrace);
    EXPECT_EQ(numeraire::utils::Logger::Core().level(), spdlog::level::trace);
}

#endif  // !defined(_WIN32)

TEST_F(LoggerTest, ReInitIsSafe) {
    numeraire::utils::Logger::Init(numeraire::utils::LogLevel::kWarn);
    EXPECT_EQ(numeraire::utils::Logger::Core().level(), spdlog::level::warn);
    numeraire::utils::Logger::Init(numeraire::utils::LogLevel::kDebug);
    EXPECT_EQ(numeraire::utils::Logger::Core().level(), spdlog::level::debug);
}
