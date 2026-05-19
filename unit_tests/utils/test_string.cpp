#include <gtest/gtest.h>

#include <numeraire/utils/string.hpp>

namespace numeraire::utils {
namespace {

TEST(StringUtils, TrimAsciiNoChangeWhenClean) {
    EXPECT_EQ(TrimAscii("2026-05-01"), "2026-05-01");
}

TEST(StringUtils, TrimAsciiStripsEdges) {
    EXPECT_EQ(TrimAscii("  hello \t"), "hello");
    EXPECT_EQ(TrimAscii("\n2026-05-01\r"), "2026-05-01");
}

TEST(StringUtils, TrimAsciiAllWhitespaceBecomesEmpty) {
    EXPECT_TRUE(TrimAscii("   ").empty());
}

TEST(StringUtils, TrimCopyMatchesTrimAscii) {
    EXPECT_EQ(TrimCopy("  x  "), "x");
    EXPECT_EQ(TrimCopy(std::string_view{"  y  "}), "y");
}

TEST(StringUtils, ToLowerAscii) {
    EXPECT_EQ(ToLowerAscii("CALL"), "call");
    EXPECT_EQ(ToLowerAscii("Db"), "db");
    EXPECT_EQ(ToLowerAscii(std::string_view{"  WARN  "}), "  warn  ");
}

TEST(StringUtils, ChainedTrimAndLower) {
    EXPECT_EQ(ToLowerAscii(TrimCopy("  EUROPEAN  ")), "european");
}

}  // namespace
}  // namespace numeraire::utils
