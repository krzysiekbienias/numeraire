#include <numeraire/simulation/normal_random.hpp>

#include <numeraire/utils/exception.hpp>

#include <cmath>
#include <limits>
#include <numbers>
#include <span>

namespace numeraire::simulation {

StandardNormalGenerator::StandardNormalGenerator(IRandomEngine* const engine) : engine_(engine) {
    if (engine_ == nullptr) {
        throw ValidationError("StandardNormalGenerator: engine must not be null");
    }
}

void StandardNormalGenerator::DrawPair(double& z0, double& z1) {
    constexpr double kMinUniform = std::numeric_limits<double>::min();
    double u1 = 0.0;
    double u2 = 0.0;
    do {
        u1 = engine_->NextUniform();
        u2 = engine_->NextUniform();
    } while (u1 <= kMinUniform);

    const double radius = std::sqrt(-2.0 * std::log(u1));
    const double angle = 2.0 * std::numbers::pi * u2;
    z0 = radius * std::cos(angle);
    z1 = radius * std::sin(angle);
}

double StandardNormalGenerator::Next() {
    if (has_spare_) {
        has_spare_ = false;
        return spare_;
    }
    double z0 = 0.0;
    double z1 = 0.0;
    DrawPair(z0, z1);
    spare_ = z1;
    has_spare_ = true;
    return z0;
}

void StandardNormalGenerator::Fill(const std::span<double> out) {
    std::size_t i = 0;
    if (has_spare_ && !out.empty()) {
        out[0] = spare_;
        has_spare_ = false;
        i = 1;
    }
    for (; i + 1 < out.size(); i += 2) {
        DrawPair(out[i], out[i + 1]);
    }
    if (i < out.size()) {
        double z0 = 0.0;
        double z1 = 0.0;
        DrawPair(z0, z1);
        out[i] = z0;
        spare_ = z1;
        has_spare_ = true;
    }
}

}  // namespace numeraire::simulation
