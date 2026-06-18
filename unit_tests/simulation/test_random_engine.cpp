#include <gtest/gtest.h>

#include <numeraire/simulation/random_engine.hpp>

#include <cstddef>
#include <vector>

namespace {

using numeraire::simulation::MersenneTwisterEngine;

std::vector<double> Draw(MersenneTwisterEngine& engine, const std::size_t count) {
    std::vector<double> out(count);
    for (double& value : out) {
        value = engine.NextUniform();
    }
    return out;
}

TEST(MersenneTwisterEngineTest, SameSeedSameSequence) {
    MersenneTwisterEngine a(42);
    MersenneTwisterEngine b(42);
    EXPECT_EQ(Draw(a, 1000), Draw(b, 1000));
}

TEST(MersenneTwisterEngineTest, DifferentSeedDiffers) {
    MersenneTwisterEngine a(1);
    MersenneTwisterEngine b(2);
    EXPECT_NE(Draw(a, 1000), Draw(b, 1000));
}

TEST(MersenneTwisterEngineTest, DifferentStreamDiffers) {
    auto a = MersenneTwisterEngine::ForStream(7, 0);
    auto b = MersenneTwisterEngine::ForStream(7, 1);
    EXPECT_NE(Draw(a, 1000), Draw(b, 1000));
}

TEST(MersenneTwisterEngineTest, SameStreamReproducible) {
    auto a = MersenneTwisterEngine::ForStream(7, 3);
    auto b = MersenneTwisterEngine::ForStream(7, 3);
    EXPECT_EQ(Draw(a, 1000), Draw(b, 1000));
}

TEST(MersenneTwisterEngineTest, UniformsInUnitInterval) {
    MersenneTwisterEngine engine(12345);
    for (std::size_t i = 0; i < 100000; ++i) {
        const double u = engine.NextUniform();
        EXPECT_GE(u, 0.0);
        EXPECT_LT(u, 1.0);
    }
}

TEST(MersenneTwisterEngineTest, FillMatchesSequential) {
    MersenneTwisterEngine a(99);
    MersenneTwisterEngine b(99);
    std::vector<double> filled(16);
    a.FillUniform(filled);
    EXPECT_EQ(filled, Draw(b, 16));
}

TEST(MersenneTwisterEngineTest, SampleMeanApproxOneHalf) {
    MersenneTwisterEngine engine(2024);
    const std::size_t n = 200000;
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        sum += engine.NextUniform();
    }
    const double mean = sum / static_cast<double>(n);
    EXPECT_NEAR(mean, 0.5, 0.005);
}

}  // namespace
