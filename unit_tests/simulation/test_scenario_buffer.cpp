#include <gtest/gtest.h>

#include <numeraire/simulation/scenario_buffer.hpp>
#include <numeraire/utils/exception.hpp>

#include <cstddef>
#include <span>

namespace {

using numeraire::simulation::ScenarioBuffer;

TEST(ScenarioBufferTest, DimensionsAndSize) {
    const ScenarioBuffer buf(2, 3, 4);
    EXPECT_EQ(buf.NumFactors(), 2U);
    EXPECT_EQ(buf.NumSteps(), 3U);
    EXPECT_EQ(buf.NumPaths(), 4U);
    EXPECT_EQ(buf.Size(), 24U);
}

TEST(ScenarioBufferTest, IndexMappingKnownOffsets) {
    const ScenarioBuffer buf(2, 3, 4);  // F=2, K=3, N=4
    EXPECT_EQ(buf.Index(0, 0, 0), 0U);
    EXPECT_EQ(buf.Index(0, 0, 3), 3U);
    EXPECT_EQ(buf.Index(0, 1, 0), 4U);
    EXPECT_EQ(buf.Index(1, 0, 0), 12U);
    EXPECT_EQ(buf.Index(1, 2, 3), 23U);
}

TEST(ScenarioBufferTest, InitializedToZero) {
    const ScenarioBuffer buf(2, 2, 2);
    for (std::size_t f = 0; f < buf.NumFactors(); ++f) {
        for (std::size_t k = 0; k < buf.NumSteps(); ++k) {
            for (std::size_t p = 0; p < buf.NumPaths(); ++p) {
                EXPECT_DOUBLE_EQ(buf.At(f, k, p), 0.0);
            }
        }
    }
}

TEST(ScenarioBufferTest, SlabHasPathLengthAndStartsAtIndex) {
    ScenarioBuffer buf(3, 4, 5);
    for (std::size_t f = 0; f < buf.NumFactors(); ++f) {
        for (std::size_t k = 0; k < buf.NumSteps(); ++k) {
            const std::span<double> slab = buf.Slab(f, k);
            EXPECT_EQ(slab.size(), buf.NumPaths());
            EXPECT_EQ(slab.data(), &buf.At(f, k, 0));
        }
    }
}

TEST(ScenarioBufferTest, AtAliasesSlab) {
    ScenarioBuffer buf(2, 2, 3);
    buf.Slab(1, 1)[2] = 42.0;
    EXPECT_DOUBLE_EQ(buf.At(1, 1, 2), 42.0);
    buf.At(0, 1, 0) = 7.0;
    EXPECT_DOUBLE_EQ(buf.Slab(0, 1)[0], 7.0);
}

TEST(ScenarioBufferTest, SlabsAreContiguousAndNonOverlapping) {
    ScenarioBuffer buf(2, 3, 4);
    for (std::size_t f = 0; f < buf.NumFactors(); ++f) {
        for (std::size_t k = 0; k < buf.NumSteps(); ++k) {
            for (std::size_t p = 0; p < buf.NumPaths(); ++p) {
                buf.At(f, k, p) = static_cast<double>(buf.Index(f, k, p));
            }
        }
    }
    for (std::size_t f = 0; f < buf.NumFactors(); ++f) {
        for (std::size_t k = 0; k < buf.NumSteps(); ++k) {
            const std::span<const double> slab = buf.Slab(f, k);
            const double base = static_cast<double>(buf.Index(f, k, 0));
            for (std::size_t p = 0; p < slab.size(); ++p) {
                EXPECT_DOUBLE_EQ(slab[p], base + static_cast<double>(p));
            }
        }
    }
}

TEST(ScenarioBufferTest, ZeroDimensionThrows) {
    EXPECT_THROW(ScenarioBuffer(0, 3, 4), numeraire::ValidationError);
    EXPECT_THROW(ScenarioBuffer(2, 0, 4), numeraire::ValidationError);
    EXPECT_THROW(ScenarioBuffer(2, 3, 0), numeraire::ValidationError);
}

}  // namespace
