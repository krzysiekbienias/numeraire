#include <numeraire/simulation/scenario_buffer.hpp>

#include <numeraire/utils/exception.hpp>

#include <limits>
#include <string>

namespace numeraire::simulation {
namespace {

[[nodiscard]] std::size_t CheckedProduct(const std::size_t a, const std::size_t b, const char* what) {
    if (a != 0 && b > std::numeric_limits<std::size_t>::max() / a) {
        throw ValidationError(std::string("ScenarioBuffer: size overflow computing ") + what);
    }
    return a * b;
}

}  // namespace

ScenarioBuffer::ScenarioBuffer(const std::size_t num_factors, const std::size_t num_steps,
                               const std::size_t num_paths)
    : num_factors_(num_factors), num_steps_(num_steps), num_paths_(num_paths) {
    if (num_factors == 0 || num_steps == 0 || num_paths == 0) {
        throw ValidationError("ScenarioBuffer: num_factors, num_steps, num_paths must all be > 0");
    }
    const std::size_t factors_steps = CheckedProduct(num_factors, num_steps, "factors*steps");
    const std::size_t total = CheckedProduct(factors_steps, num_paths, "factors*steps*paths");
    data_.assign(total, 0.0);
}

}  // namespace numeraire::simulation
