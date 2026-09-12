#pragma once

#include <numeraire/enums/option_type.hpp>
#include <string_view>

#include "numeraire/schedule/date.hpp"

namespace numeraire::core {

/// Read-only market snapshot for pricing. Implementations live outside `core`
/// (static provider, DB, live feed, …).
///
/// Conventions (document for each concrete provider):
/// — `ValuationDate`: calendar date for which quotes and other inputs are valid
///   (EOD as-of). Time to expiry in pricers is from `ValuationDate` to product
///   `ExpiryDate()`, on Act/365 Fixed unless documented otherwise.
/// — `Quote(id)`: observed mark for that key — equity/index close, or a dated
///   futures settle when `id` is a contract ticker. Not cash commodity spot.
/// — `RiskFreeRate`: continuous or simple; pricer and model must match.
/// — `ImpliedVolatility(underlying, strike, T)`: flat or surface slice; `T` is
///   year fraction in the same basis the pricer uses for that slice.
class IMarketData {
protected:
    IMarketData() = default;

public:
    virtual ~IMarketData() = default;

    IMarketData(const IMarketData&) = delete;
    IMarketData& operator=(const IMarketData&) = delete;
    IMarketData(IMarketData&&) = delete;
    IMarketData& operator=(IMarketData&&) = delete;

    [[nodiscard]] virtual const schedule::Date& ValuationDate() const = 0;
    // Quote is a Spot or Future or Forward - generic name on underlier's market price
    [[nodiscard]] virtual double Quote(std::string_view underlying_id) const = 0;

    [[nodiscard]] virtual double RiskFreeRate() const = 0;

    /// Continuous zero rate for tenor `time_to_expiry_years` (curve interpolation when wired).
    [[nodiscard]] virtual double RiskFreeRateForTenor(const double time_to_expiry_years) const {
        return RiskFreeRate();
    }

    [[nodiscard]] virtual double DividendYield(std::string_view underlying_id) const = 0;

    /// Black–Scholes-style vol for \((K, T)\) and option side (call/put surface leg).
    [[nodiscard]] virtual double ImpliedVolatility(std::string_view underlying_id,
                                                   double strike,
                                                   double time_to_expiry_years,
                                                   OptionType option_kind) const = 0;
};

}  // namespace numeraire::core
