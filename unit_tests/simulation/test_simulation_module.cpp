#include <gtest/gtest.h>

#include <numeraire/simulation/simulation_module.hpp>

namespace {

TEST(SimulationModuleTest, Builds) {
    EXPECT_EQ(numeraire::simulation::ModuleName(), "simulation");
}

}  // namespace
