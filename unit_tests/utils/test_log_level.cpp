#include <numeraire/utils/log_level.hpp>

#include <gtest/gtest.h>

namespace numeraire::utils {

TEST(LogLevel, ParsesCaseInsensitive) {
    ASSERT_EQ(TryParseLogLevel("INFO"), LogLevel::kInfo);
    ASSERT_EQ(TryParseLogLevel("info"), LogLevel::kInfo);
    ASSERT_EQ(TryParseLogLevel("  WARN "), LogLevel::kWarn);
    ASSERT_EQ(TryParseLogLevel("warning"), LogLevel::kWarn);
    ASSERT_EQ(TryParseLogLevel("fatal"), LogLevel::kCritical);
}

TEST(LogLevel, ReturnsNulloptForUnknown) {
    EXPECT_FALSE(TryParseLogLevel("").has_value());
    EXPECT_FALSE(TryParseLogLevel("   ").has_value());
    EXPECT_FALSE(TryParseLogLevel("banana").has_value());
}

}  // namespace numeraire::utils
