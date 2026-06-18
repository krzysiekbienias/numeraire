#include <numeraire/simulation/scenario_buffer.hpp>

#include <numeraire/utils/exception.hpp>

#include <algorithm>
#include <limits>
#include <memory_resource>
#include <string>

namespace numeraire::simulation {
namespace {

[[nodiscard]] std::size_t CheckedProduct(const std::size_t a, const std::size_t b, const char* what) {
    if (a != 0 && b > std::numeric_limits<std::size_t>::max() / a) {
        throw ValidationError(std::string("ScenarioBuffer: size overflow computing ") + what);
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

ScenarioBuffer::ScenarioBuffer(const std::size_t num_factors, const std::size_t num_steps,
                               const std::size_t num_paths, std::pmr::memory_resource* upstream)
    : num_factors_(num_factors),
      num_steps_(num_steps),
      num_paths_(num_paths),
      stride_(RoundUpToMultiple(num_paths, kPathAlignment)),
      arena_(upstream != nullptr ? upstream : std::pmr::new_delete_resource()),
      data_(nullptr) {
    if (num_factors == 0 || num_steps == 0 || num_paths == 0) {
        throw ValidationError("ScenarioBuffer: num_factors, num_steps, num_paths must all be > 0");
    }
    const std::size_t rows = CheckedProduct(num_factors, num_steps, "factors*steps");
    const std::size_t total_elems = CheckedProduct(rows, stride_, "factors*steps*stride");
    const std::size_t total_bytes = CheckedProduct(total_elems, sizeof(double), "byte size");
    data_ = static_cast<double*>(arena_.allocate(total_bytes, kCacheLineBytes));
    std::fill_n(data_, total_elems, 0.0);
}

}  // namespace numeraire::simulation
