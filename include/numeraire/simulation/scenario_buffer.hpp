#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace numeraire::simulation {

/// Struct-of-Arrays storage for simulated risk-factor paths.
///
/// Logical shape is `[factor F][step K][path N]`. Values live in one flat block
/// with **contiguous (factor, step) slabs**: the `N` path values for a given
/// `(factor, step)` are adjacent in memory, so both the GBM evolution kernel and
/// path-wise pricing stream over paths sequentially (vectorizable, cache-friendly).
///
/// Flat index: `offset = ((factor * num_steps) + step) * num_paths + path`.
///
/// Step 1 backs storage with `std::vector<double>`; a later step swaps the
/// backing store to a cache-line-aligned `std::pmr::monotonic_buffer_resource`
/// arena **without changing this API**.
class ScenarioBuffer {
   public:
    ScenarioBuffer(std::size_t num_factors, std::size_t num_steps, std::size_t num_paths);

    [[nodiscard]] std::size_t NumFactors() const noexcept { return num_factors_; }
    [[nodiscard]] std::size_t NumSteps() const noexcept { return num_steps_; }
    [[nodiscard]] std::size_t NumPaths() const noexcept { return num_paths_; }
    [[nodiscard]] std::size_t Size() const noexcept { return data_.size(); }

    /// Flat offset of `(factor, step, path)`. No bounds checking (hot path).
    [[nodiscard]] std::size_t Index(const std::size_t factor, const std::size_t step,
                                    const std::size_t path) const noexcept {
        return (((factor * num_steps_) + step) * num_paths_) + path;
    }

    /// View over the `N` path values for one `(factor, step)` slab.
    [[nodiscard]] std::span<double> Slab(const std::size_t factor, const std::size_t step) noexcept {
        return std::span<double>(data_.data() + Index(factor, step, 0), num_paths_);
    }

    [[nodiscard]] std::span<const double> Slab(const std::size_t factor,
                                               const std::size_t step) const noexcept {
        return std::span<const double>(data_.data() + Index(factor, step, 0), num_paths_);
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
    std::vector<double> data_;
};

}  // namespace numeraire::simulation
