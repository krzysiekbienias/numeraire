#pragma once

#include <numeraire/simulation/random_engine.hpp>

#include <span>

namespace numeraire::simulation {

/// Draws independent standard normals `N(0, 1)` from an `IRandomEngine` via the
/// Box–Muller transform (two uniforms per pair). Caches one spare value so odd
/// batch sizes and sequential `Next()` calls stay consistent.
class StandardNormalGenerator {
   public:
    explicit StandardNormalGenerator(IRandomEngine* engine);

    [[nodiscard]] double Next();

    /// Fill `out` with standard normals — same sequence as calling `Next()` once
    /// per element (including spare-cache behaviour).
    void Fill(std::span<double> out);

   private:
    void DrawPair(double& z0, double& z1);

    IRandomEngine* engine_;
    bool has_spare_{false};
    double spare_{0.0};
};

}  // namespace numeraire::simulation
