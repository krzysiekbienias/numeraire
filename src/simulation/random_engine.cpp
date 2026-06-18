#include <numeraire/simulation/random_engine.hpp>

#include <cstdint>
#include <limits>
#include <random>
#include <span>

namespace numeraire::simulation {
namespace {

constexpr std::uint64_t kLow32Mask = 0xFFFFFFFFULL;

[[nodiscard]] std::mt19937 SeedMersenne(const std::uint64_t seed, const std::uint64_t stream_index) {
    std::seed_seq sequence{
        static_cast<std::uint32_t>(seed & kLow32Mask),
        static_cast<std::uint32_t>(seed >> 32U),
        static_cast<std::uint32_t>(stream_index & kLow32Mask),
        static_cast<std::uint32_t>(stream_index >> 32U),
    };
    return std::mt19937(sequence);
}

}  // namespace

MersenneTwisterEngine::MersenneTwisterEngine(const std::uint64_t seed, const std::uint64_t stream_index)
    : engine_(SeedMersenne(seed, stream_index)) {}

MersenneTwisterEngine MersenneTwisterEngine::ForStream(const std::uint64_t base_seed,
                                                       const std::uint64_t stream_index) {
    return MersenneTwisterEngine(base_seed, stream_index);
}

double MersenneTwisterEngine::NextUniform() {
    return std::generate_canonical<double, std::numeric_limits<double>::digits>(engine_);
}

void MersenneTwisterEngine::FillUniform(std::span<double> out) {
    for (double& value : out) {
        value = NextUniform();
    }
}

}  // namespace numeraire::simulation
