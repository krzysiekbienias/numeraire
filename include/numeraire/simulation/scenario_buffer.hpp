#pragma once

#include <cstddef>
#include <memory_resource>
#include <span>

namespace numeraire::simulation {

/// Struct-of-Arrays storage for simulated risk-factor paths.
///
/// Logical shape is `[factor F][step K][path N]`. Values live in one flat block
/// with **contiguous (factor, step) slabs**: the `N` path values for a given
/// `(factor, step)` are adjacent in memory, so both the GBM evolution kernel and
/// path-wise pricing stream over paths sequentially (vectorizable, cache-friendly).
///
/// Storage comes from a `std::pmr::monotonic_buffer_resource` arena (bump-pointer
/// allocate, bulk free on destruction). The block is requested **cache-line
/// aligned** and each row is padded to a `Stride()` that is a multiple of
/// `kPathAlignment`, so **every** slab starts on a 64-byte boundary (aligned SIMD
/// loads; no false sharing once paths are processed across threads).
///
/// Flat index: `offset = ((factor * num_steps) + step) * stride + path`.
class ScenarioBuffer {
   public:
    /// Cache-line size used to align each `(factor, step)` slab.
    static constexpr std::size_t kCacheLineBytes = 64;
    /// Path padding granularity in `double`s (one cache line worth).
    static constexpr std::size_t kPathAlignment = kCacheLineBytes / sizeof(double);

    explicit ScenarioBuffer(std::size_t num_factors, std::size_t num_steps, std::size_t num_paths,
                            std::pmr::memory_resource* upstream = std::pmr::new_delete_resource());

    ScenarioBuffer(const ScenarioBuffer&) = delete;
    ScenarioBuffer& operator=(const ScenarioBuffer&) = delete;
    ScenarioBuffer(ScenarioBuffer&&) = delete;
    ScenarioBuffer& operator=(ScenarioBuffer&&) = delete;
    ~ScenarioBuffer() = default;

    [[nodiscard]] std::size_t NumFactors() const noexcept { return num_factors_; }
    [[nodiscard]] std::size_t NumSteps() const noexcept { return num_steps_; }
    [[nodiscard]] std::size_t NumPaths() const noexcept { return num_paths_; }

    /// Padded paths per row (`>= NumPaths`, multiple of `kPathAlignment`) that
    /// keeps each slab cache-line aligned.
    [[nodiscard]] std::size_t Stride() const noexcept { return stride_; }

    /// Logical count of meaningful values (`NumFactors * NumSteps * NumPaths`).
    [[nodiscard]] std::size_t Size() const noexcept {
        return num_factors_ * num_steps_ * num_paths_;
    }

    /// Allocated element count including per-row padding (`NumFactors * NumSteps * Stride`).
    [[nodiscard]] std::size_t AllocatedElems() const noexcept {
        return num_factors_ * num_steps_ * stride_;
    }

    /// Flat offset of `(factor, step, path)`. No bounds checking (hot path).
    [[nodiscard]] std::size_t Index(const std::size_t factor, const std::size_t step,
                                    const std::size_t path) const noexcept {
        return (((factor * num_steps_) + step) * stride_) + path;
    }

    /// View over the `N` path values for one `(factor, step)` slab.
    [[nodiscard]] std::span<double> Slab(const std::size_t factor, const std::size_t step) noexcept {
        return std::span<double>(data_ + Index(factor, step, 0), num_paths_);
    }

    [[nodiscard]] std::span<const double> Slab(const std::size_t factor,
                                               const std::size_t step) const noexcept {
        return std::span<const double>(data_ + Index(factor, step, 0), num_paths_);
    }

    [[nodiscard]] double& At(const std::size_t factor, const std::size_t step,
                             const std::size_t path) noexcept {
        return data_[Index(factor, step, path)];
    }

    [[nodiscard]] double At(const std::size_t factor, const std::size_t step,
                            const std::size_t path) const noexcept {
        return data_[Index(factor, step, path)];
    }

   private:
    std::size_t num_factors_;
    std::size_t num_steps_;
    std::size_t num_paths_;
    std::size_t stride_;
    std::pmr::monotonic_buffer_resource arena_;
    double* data_;
};

}  // namespace numeraire::simulation
