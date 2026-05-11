#pragma once

#include <numeraire/enums/exercise_style.hpp>
#include <numeraire/enums/option_type.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/schedule/schedule.hpp>

#include <string_view>

namespace numeraire::core {

/// Contract for a tradable / pricable instrument. Implementations live
/// outside `core` (structs from DB, payoff libraries, …).
class IProduct {
   protected:
    IProduct() = default;

   public:
    virtual ~IProduct() = default;

    IProduct(const IProduct&) = delete;
    IProduct& operator=(const IProduct&) = delete;
    IProduct(IProduct&&) = delete;
    IProduct& operator=(IProduct&&) = delete;

    [[nodiscard]] virtual std::string_view UnderlyingId() const = 0;

    [[nodiscard]] virtual OptionType OptionKind() const = 0;

    [[nodiscard]] virtual ExerciseStyle Exercise() const = 0;

    [[nodiscard]] virtual double Strike() const = 0;

    [[nodiscard]] virtual const schedule::Date& TradeDate() const = 0;

    [[nodiscard]] virtual const schedule::Date& ExpiryDate() const = 0;

    /// Optional cashflow / fixing grid. `nullptr` for **bullet** profiles
    /// (e.g. European vanilla settled at `ExpiryDate()` only).
    [[nodiscard]] virtual const schedule::Schedule* PaymentSchedule() const = 0;
};

}  // namespace numeraire::core
