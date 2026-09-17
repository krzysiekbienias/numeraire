#pragma once

#include <numeraire/core/iproduct.hpp>
#include <numeraire/enums/exercise_style.hpp>
#include <numeraire/enums/option_type.hpp>
#include <numeraire/schedule/date.hpp>
#include <numeraire/schedule/schedule.hpp>
#include <optional>
#include <string>

namespace numeraire::products {

/// European **asset-or-nothing** binary on equity: at expiry pays **spot** \f$S_T\f$
/// if ITM (call: \f$S_T > K\f$, put: \f$S_T < K\f$), otherwise zero — **not** the
/// vanilla \f$(S_T - K)^+\f$. Same `IProduct` surface as vanilla so calendars and
/// hooks align; **pricers must branch on concrete type** (or a future
/// `InstrumentKind`) rather than assuming BS delta-gamma on \f$(S-K)^+\f$.
class EquityAssetOrNothingProduct final : public core::IProduct {
public:
    EquityAssetOrNothingProduct(std::string underlying_id,
                                OptionType kind,
                                ExerciseStyle exercise,
                                double strike,
                                schedule::Date trade_date,
                                schedule::Date expiry_date,
                                std::optional<schedule::Schedule> payments = std::nullopt);

    [[nodiscard]] std::string_view UnderlyingId() const override;

    [[nodiscard]] OptionType OptionKind() const override;

    [[nodiscard]] ExerciseStyle Exercise() const override;

    [[nodiscard]] double Strike() const override;

    [[nodiscard]] const schedule::Date& TradeDate() const override;

    [[nodiscard]] const schedule::Date& ExpiryDate() const override;

    [[nodiscard]] const schedule::Schedule* PaymentSchedule() const override;

    [[nodiscard]] bool UsesImpliedVolatility() const override;

    [[nodiscard]] bool HasCalendarMaturity() const override;

private:
    std::string underlying_id_;
    OptionType kind_;
    ExerciseStyle exercise_;
    double strike_;
    schedule::Date trade_date_;
    schedule::Date expiry_date_;
    std::optional<schedule::Schedule> payments_;
};

}  // namespace numeraire::products
