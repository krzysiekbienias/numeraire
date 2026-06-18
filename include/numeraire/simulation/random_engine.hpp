#pragma once

#include <cstdint>
#include <random>
#include <span>

namespace numeraire::simulation {

/// Abstraction over a source of uniform variates in `[0, 1)`. This is the swap
/// point between a pseudo-random engine (Mersenne Twister today) and a
/// quasi-random sequence (Sobol later) -- both ultimately produce points in
/// `[0, 1)`, so normal generation and correlation layer on top of this seam.
class IRandomEngine {
   protected:
    IRandomEngine() = default;

   public:
    virtual ~IRandomEngine() = default;

    IRandomEngine(const IRandomEngine&) = delete;
    IRandomEngine& operator=(const IRandomEngine&) = delete;
    IRandomEngine(IRandomEngine&&) = delete;
    IRandomEngine& operator=(IRandomEngine&&) = delete;

    /// Next uniform variate in `[0, 1)`.
    [[nodiscard]] virtual double NextUniform() = 0;

    /// Fill `out` with uniform variates in `[0, 1)` -- the same draws as calling
    /// `NextUniform()` once per element.
    virtual void FillUniform(std::span<double> out) = 0;
};

/// Classic Mersenne Twister (`std::mt19937`) engine yielding uniforms in `[0, 1)`
/// via `std::generate_canonical` (guaranteed `< 1`). Seeding folds a 64-bit base
/// seed and a stream index through a `std::seed_seq`, giving reproducible,
/// independent streams per path block (lock-free parallelism: one engine per
/// block).
class MersenneTwisterEngine final : public IRandomEngine {
   public:
    explicit MersenneTwisterEngine(std::uint64_t seed, std::uint64_t stream_index = 0);

    /// Named factory for the per-block stream pattern (same as the constructor).
    [[nodiscard]] static MersenneTwisterEngine ForStream(std::uint64_t base_seed,
                                                         std::uint64_t stream_index);

    [[nodiscard]] double NextUniform() override;
    void FillUniform(std::span<double> out) override;

   private:
    std::mt19937 engine_;
};

}  // namespace numeraire::simulation
