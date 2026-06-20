#pragma once

#include <cstddef>
#include <memory_resource>
#include <span>

namespace numeraire::simulation {

/// Struct-of-Arrays storage for path-wise leg PV totals.
///
/// Logical shape `[leg L][step K][path N]` with the same padded stride layout as
/// `ScenarioBuffer` so path-wise pricing can stream contiguous slabs.
class LegPathPvBuffer {
   public:
    static constexpr std::size_t kCacheLineBytes = 64;
    static constexpr std::size_t kPathAlignment = kCacheLineBytes / sizeof(double);

    explicit LegPathPvBuffer(std::size_t num_legs, std::size_t num_steps, std::size_t num_paths,
                             std::pmr::memory_resource* upstream = std::pmr::new_delete_resource());

    LegPathPvBuffer(const LegPathPvBuffer&) = delete;
    LegPathPvBuffer& operator=(const LegPathPvBuffer&) = delete;
    LegPathPvBuffer(LegPathPvBuffer&&) = delete;
    LegPathPvBuffer& operator=(LegPathPvBuffer&&) = delete;
    ~LegPathPvBuffer() = default;

    [[nodiscard]] std::size_t NumLegs() const noexcept { return num_legs_; }
    [[nodiscard]] std::size_t NumSteps() const noexcept { return num_steps_; }
    [[nodiscard]] std::size_t NumPaths() const noexcept { return num_paths_; }
    [[nodiscard]] std::size_t Stride() const noexcept { return stride_; }

    [[nodiscard]] std::size_t Index(std::size_t leg, std::size_t step, std::size_t path) const noexcept {
        return (((leg * num_steps_) + step) * stride_) + path;
    }

    [[nodiscard]] std::span<double> Slab(std::size_t leg, std::size_t step) noexcept {
        return std::span<double>(data_ + Index(leg, step, 0), num_paths_);
    }

    [[nodiscard]] std::span<const double> Slab(std::size_t leg, std::size_t step) const noexcept {
        return std::span<const double>(data_ + Index(leg, step, 0), num_paths_);
    }

    [[nodiscard]] double& At(std::size_t leg, std::size_t step, std::size_t path) noexcept {
        return data_[Index(leg, step, path)];
    }

    [[nodiscard]] double At(std::size_t leg, std::size_t step, std::size_t path) const noexcept {
        return data_[Index(leg, step, path)];
    }

   private:
    std::size_t num_legs_;
    std::size_t num_steps_;
    std::size_t num_paths_;
    std::size_t stride_;
    std::pmr::monotonic_buffer_resource arena_;
    double* data_;
};

}  // namespace numeraire::simulation
