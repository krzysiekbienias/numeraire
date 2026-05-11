#pragma once

#include <numeraire/enums/model_type.hpp>

namespace numeraire::core {

/// Chosen / calibrated dynamics for a pricing run (see `ModelType`). Distinct
/// from `IMarketData`: the market supplies **quotes**; the model holds **state
/// used by the stochastic / PDE formulation** for this invocation (possibly
/// fitted to those quotes off-line).
///
/// Implementations live outside `core` (factories, calibration, …).
class IModel {
   protected:
    IModel() = default;

   public:
    virtual ~IModel() = default;

    IModel(const IModel&) = delete;
    IModel& operator=(const IModel&) = delete;
    IModel(IModel&&) = delete;
    IModel& operator=(IModel&&) = delete;

    [[nodiscard]] virtual ModelType Kind() const = 0;

    /// Effective **constant** lognormal volatility for Black–Scholes–style
    /// analytics. Pricers **must** branch on `Kind()` before using this value.
    /// For other model families, return `std::numeric_limits<double>::quiet_NaN()`
    /// until specialized accessors exist.
    [[nodiscard]] virtual double FlatVolatility() const = 0;
};

}  // namespace numeraire::core
