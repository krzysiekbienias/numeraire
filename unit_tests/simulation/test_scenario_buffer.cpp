#include <gtest/gtest.h>

#include <numeraire/simulation/scenario_buffer.hpp>
#include <numeraire/utils/exception.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

namespace {

using numeraire::simulation::ScenarioBuffer;

TEST(ScenarioBufferTest, DimensionsAndSize) {
    const ScenarioBuffer buf(2, 3, 4);
    EXPECT_EQ(buf.NumFactors(), 2U);
    EXPECT_EQ(buf.NumSteps(), 3U);
    EXPECT_EQ(buf.NumPaths(), 4U);
    EXPECT_EQ(buf.Size(), 24U);  // logical F*K*N, independent of padding
}

TEST(ScenarioBufferTest, StridePadsPathsToCacheLine) {
    EXPECT_EQ(ScenarioBuffer::kPathAlignment, 8U);  // 64 bytes / sizeof(double)
    EXPECT_EQ(ScenarioBuffer(1, 1, 1).Stride(), 8U);
    EXPECT_EQ(ScenarioBuffer(1, 1, 5).Stride(), 8U);
    EXPECT_EQ(ScenarioBuffer(1, 1, 8).Stride(), 8U);
    EXPECT_EQ(ScenarioBuffer(1, 1, 9).Stride(), 16U);
    EXPECT_EQ(ScenarioBuffer(1, 1, 16).Stride(), 16U);
}

TEST(ScenarioBufferTest, AllocatedElemsIncludePadding) {
    const ScenarioBuffer buf(2, 3, 4);  // stride padded 4 -> 8
    EXPECT_EQ(buf.Stride(), 8U);
    EXPECT_EQ(buf.AllocatedElems(), 2U * 3U * 8U);
}

TEST(ScenarioBufferTest, IndexMappingUsesStride) {
    const ScenarioBuffer buf(2, 3, 4);  // F=2, K=3, N=4, stride=8
    ASSERT_EQ(buf.Stride(), 8U);
    EXPECT_EQ(buf.Index(0, 0, 0), 0U);
    EXPECT_EQ(buf.Index(0, 0, 3), 3U);
    EXPECT_EQ(buf.Index(0, 1, 0), 8U);   // next row starts at one stride
    EXPECT_EQ(buf.Index(1, 0, 0), 24U);  // ((1*3)+0)*8
    EXPECT_EQ(buf.Index(1, 2, 3), 43U);  // ((1*3)+2)*8 + 3
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

TEST(ScenarioBufferTest, EverySlabIsCacheLineAligned) {
    ScenarioBuffer buf(3, 4, 5);  // N=5 -> stride 8, padding exercised
    for (std::size_t f = 0; f < buf.NumFactors(); ++f) {
        for (std::size_t k = 0; k < buf.NumSteps(); ++k) {
            const auto address = std::bit_cast<std::uintptr_t>(buf.Slab(f, k).data());
            EXPECT_EQ(address % ScenarioBuffer::kCacheLineBytes, 0U);
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

TEST(ScenarioBufferTest, SlabsAreContiguousWithinRow) {
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
