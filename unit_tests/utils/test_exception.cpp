#include <numeraire/utils/exception.hpp>

#include <gtest/gtest.h>

namespace numeraire {

TEST(Exception, TypesAreDistinct) {
    EXPECT_THROW(throw ConfigError("cfg");, ConfigError);
    EXPECT_THROW(throw ValidationError("val");, ValidationError);
    EXPECT_THROW(throw ScheduleError("sch");, ScheduleError);
    EXPECT_THROW(throw PricingError("prc");, PricingError);
    EXPECT_THROW(throw MarketDataError("mkt");, MarketDataError);
    EXPECT_THROW(throw PersistenceError("per");, PersistenceError);
}

TEST(Exception, CaughtAsBase) {
    try {
        throw ConfigError("missing key");
    } catch (const NumeraireException& ex) {
        EXPECT_STREQ(ex.what(), "missing key");
        return;
    }
    FAIL() << "Expected NumeraireException";
}

}  // namespace numeraire
