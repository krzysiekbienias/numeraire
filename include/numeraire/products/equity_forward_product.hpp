#pragma once

#include <numeraire/core/iproduct.hpp>
#include <numeraire/enums/exercise_style.hpp>
#include <numeraire/enums/option_type.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/schedule/schedule.hpp>

#include <optional>
#include <string>

namespace numeraire::products {

/// European **equity forward**: at expiry settles on forward price `Strike()` \(K\)
/// (cash or physical per catalog). `pv_unit` is **long forward** NPV:
/// \(S e^{-qT} - K e^{-rT}\); leg `direction` applies sign at position level.
/// `OptionKind()` is unused for pricing — pricers must branch on this type.
class EquityForwardProduct final : public core::IProduct {
   public:
    EquityForwardProduct(std::string underlying_id, double forward_price, schedule::Date trade_date,
                         schedule::Date expiry_date, std::optional<schedule::Schedule> payments = std::nullopt);

    [[nodiscard]] std::string_view UnderlyingId() const override;

    [[nodiscard]] OptionType OptionKind() const override;

    [[nodiscard]] ExerciseStyle Exercise() const override;

    [[nodiscard]] double Strike() const override;

    [[nodiscard]] const schedule::Date& TradeDate() const override;

    [[nodiscard]] const schedule::Date& ExpiryDate() const override;

    [[nodiscard]] const schedule::Schedule* PaymentSchedule() const override;

   private:
    std::string underlying_id_;
    double forward_price_;
    schedule::Date trade_date_;
    schedule::Date expiry_date_;
    std::optional<schedule::Schedule> payments_;
};

}  // namespace numeraire::products
