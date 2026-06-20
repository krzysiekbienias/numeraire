#include <numeraire/simulation/leg_path_pv_buffer.hpp>

#include <numeraire/utils/exception.hpp>

#include <algorithm>
#include <limits>
#include <memory_resource>
#include <string>

namespace numeraire::simulation {
namespace {

[[nodiscard]] std::size_t CheckedProduct(const std::size_t a, const std::size_t b, const char* what) {
    if (a != 0 && b > std::numeric_limits<std::size_t>::max() / a) {
        throw ValidationError(std::string("LegPathPvBuffer: size overflow computing ") + what);
    }
    return a * b;
}

[[nodiscard]] std::size_t RoundUpToMultiple(const std::size_t value, const std::size_t multiple) noexcept {
    if (multiple <= 1) {
        return value;
    }
    const std::size_t remainder = value % multiple;
    return remainder == 0 ? value : value + (multiple - remainder);
}

}  // namespace

LegPathPvBuffer::LegPathPvBuffer(const std::size_t num_legs, const std::size_t num_steps,
                                 const std::size_t num_paths, std::pmr::memory_resource* upstream)
    : num_legs_(num_legs),
      num_steps_(num_steps),
      num_paths_(num_paths),
      stride_(RoundUpToMultiple(num_paths, kPathAlignment)),
      arena_(upstream != nullptr ? upstream : std::pmr::new_delete_resource()),
      data_(nullptr) {
    if (num_legs == 0 || num_steps == 0 || num_paths == 0) {
        throw ValidationError("LegPathPvBuffer: num_legs, num_steps, num_paths must all be > 0");
    }

    const std::size_t rows = CheckedProduct(num_legs_, num_steps_, "rows");
    const std::size_t elems = CheckedProduct(rows, stride_, "elements");
    const std::size_t bytes = CheckedProduct(elems, sizeof(double), "bytes");
    void* raw = arena_.allocate(bytes, kCacheLineBytes);
    data_ = static_cast<double*>(raw);
    std::fill_n(data_, elems, 0.0);
}

}  // namespace numeraire::simulation
