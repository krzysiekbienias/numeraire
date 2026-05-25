#pragma once

#include <numeraire/core/iproduct.hpp>
#include <numeraire/enums/exercise_style.hpp>
#include <numeraire/enums/option_type.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/schedule/schedule.hpp>

#include <optional>
#include <string>

namespace numeraire::products {

/// European **cash-or-nothing** (digital) on equity: at expiry pays fixed cash
/// `CashPayoutPerShare()` (per-share scale, same as `pv_unit`) if ITM (call:
/// \(S_T > K\), put: \(S_T < K\)), otherwise zero. `Strike()` is the barrier \(K\).
class EquityCashOrNothingProduct final : public core::IProduct {
   public:
    EquityCashOrNothingProduct(std::string underlying_id, OptionType kind, ExerciseStyle exercise,
                               double strike, double cash_payout_per_share, schedule::Date trade_date,
                               schedule::Date expiry_date, std::optional<schedule::Schedule> payments = std::nullopt);

    [[nodiscard]] std::string_view UnderlyingId() const override;

    [[nodiscard]] OptionType OptionKind() const override;

    [[nodiscard]] ExerciseStyle Exercise() const override;

    [[nodiscard]] double Strike() const override;

    [[nodiscard]] double CashPayoutPerShare() const;

    [[nodiscard]] const schedule::Date& TradeDate() const override;

    [[nodiscard]] const schedule::Date& ExpiryDate() const override;

    [[nodiscard]] const schedule::Schedule* PaymentSchedule() const override;

   private:
    std::string underlying_id_;
    OptionType kind_;
    ExerciseStyle exercise_;
    double strike_;
    double cash_payout_per_share_;
    schedule::Date trade_date_;
    schedule::Date expiry_date_;
    std::optional<schedule::Schedule> payments_;
};

}  // namespace numeraire::products
