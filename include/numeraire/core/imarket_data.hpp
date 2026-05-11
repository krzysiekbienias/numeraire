#pragma once

#include <string_view>

namespace numeraire::core {

/// Read-only market snapshot for pricing. Implementations live outside `core`
/// (e.g. static provider, DB, live feed). No QuantLib or schedule types on the
/// surface so `core` headers stay free of those dependencies.
///
/// Conventions (document for each concrete provider):
/// — `RiskFreeRate`: continuous or simple; pricer + model must match.
/// — `time_to_expiry_years`: year fraction in the same basis the pricer expects
///   (often from `ScheduleGenerator::YearFraction` on the product horizon).
class IMarketData {
   protected:
    IMarketData() = default;

   public:
    virtual ~IMarketData() = default;

    IMarketData(const IMarketData&) = delete;
    IMarketData& operator=(const IMarketData&) = delete;
    IMarketData(IMarketData&&) = delete;
    IMarketData& operator=(IMarketData&&) = delete;

    [[nodiscard]] virtual double Spot(std::string_view underlying_id) const = 0;

    [[nodiscard]] virtual double RiskFreeRate() const = 0;

    [[nodiscard]] virtual double DividendYield(std::string_view underlying_id) const = 0;

    /// Flat Black–Scholes-style vol for the slice \((K, T)\). Overrides may
    /// interpolate a surface; `underlying_id` selects the name.
    [[nodiscard]] virtual double ImpliedVolatility(std::string_view underlying_id,
                                                   double strike,
                                                   double time_to_expiry_years) const = 0;
};

}  // namespace numeraire::core
