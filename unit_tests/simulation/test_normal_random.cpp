#include <gtest/gtest.h>

#include <numeraire/simulation/normal_random.hpp>
#include <numeraire/simulation/random_engine.hpp>

#include <cmath>
#include <cstddef>
#include <vector>

namespace {

using numeraire::simulation::MersenneTwisterEngine;
using numeraire::simulation::StandardNormalGenerator;

std::vector<double> DrawNormals(MersenneTwisterEngine& engine, const std::size_t count) {
    StandardNormalGenerator normals(&engine);
    std::vector<double> out(count);
    for (double& value : out) {
        value = normals.Next();
    }
    return out;
}

TEST(StandardNormalGeneratorTest, SameSeedSameSequence) {
    MersenneTwisterEngine a(42);
    MersenneTwisterEngine b(42);
    EXPECT_EQ(DrawNormals(a, 1000), DrawNormals(b, 1000));
}

TEST(StandardNormalGeneratorTest, DifferentSeedDiffers) {
    MersenneTwisterEngine a(1);
    MersenneTwisterEngine b(2);
    EXPECT_NE(DrawNormals(a, 1000), DrawNormals(b, 1000));
}

TEST(StandardNormalGeneratorTest, FillMatchesSequential) {
    MersenneTwisterEngine engine_a(99);
    MersenneTwisterEngine engine_b(99);
    StandardNormalGenerator fill_gen(&engine_a);
    StandardNormalGenerator seq_gen(&engine_b);

    std::vector<double> filled(17);  // odd length exercises spare cache
    fill_gen.Fill(filled);
    std::vector<double> sequential(17);
    for (double& value : sequential) {
        value = seq_gen.Next();
    }
    EXPECT_EQ(filled, sequential);
}

TEST(StandardNormalGeneratorTest, SampleMeanNearZero) {
    MersenneTwisterEngine engine(2024);
    StandardNormalGenerator normals(&engine);
    const std::size_t n = 200000;
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        sum += normals.Next();
    }
    const double mean = sum / static_cast<double>(n);
    EXPECT_NEAR(mean, 0.0, 0.02);
}

TEST(StandardNormalGeneratorTest, SampleVarianceNearOne) {
    MersenneTwisterEngine engine(31415);
    StandardNormalGenerator normals(&engine);
    const std::size_t n = 200000;
    double sum_sq = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double z = normals.Next();
        sum_sq += z * z;
    }
    const double variance = sum_sq / static_cast<double>(n);
    EXPECT_NEAR(variance, 1.0, 0.05);
}

TEST(StandardNormalGeneratorTest, FillSlabStatisticalSanity) {
    MersenneTwisterEngine engine(777);
    StandardNormalGenerator normals(&engine);
    std::vector<double> slab(4096);
    normals.Fill(slab);

    double sum = 0.0;
    double sum_sq = 0.0;
    for (const double z : slab) {
        sum += z;
        sum_sq += z * z;
    }
    const double n = static_cast<double>(slab.size());
    EXPECT_NEAR(sum / n, 0.0, 0.05);
    EXPECT_NEAR(sum_sq / n, 1.0, 0.08);
}

}  // namespace
